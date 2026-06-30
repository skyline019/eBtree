#include "ebtree/concept/wal/wal_batch_pipeline.h"

#include <chrono>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace ebtree {
namespace {

#if defined(_WIN32)
int64_t NowUs() {
  static LARGE_INTEGER freq = [] {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return f;
  }();
  LARGE_INTEGER c{};
  QueryPerformanceCounter(&c);
  return (c.QuadPart * 1000000) / freq.QuadPart;
}

void TuneWorkerThread() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
}
#else
void TuneWorkerThread() {}
#endif

constexpr uint32_t kMinSparseQueue = 1;
constexpr uint32_t kMinThroughputQueue = 16;
constexpr int kMaxBurstDrains = 32;

}  // namespace

WalBatchPipeline::WalBatchPipeline(WalWriter* wal, WalFsyncConfig config)
    : wal_(wal), config_(config) {
  if (config_.max_batch_size == 0) config_.max_batch_size = 512;
  if (config_.wal_batch_bytes == 0) config_.wal_batch_bytes = 16384;
#if defined(_WIN32)
  static bool once = [] {
    timeBeginPeriod(1);
    return true;
  }();
  (void)once;
#endif
  worker_ = std::thread([this] {
    TuneWorkerThread();
    WorkerLoop();
  });
}

void WalBatchPipeline::SetCommitHook(WalBatchCommitHook hook) {
  commit_hook_ = std::move(hook);
}

WalBatchPipeline::~WalBatchPipeline() {
  {
    std::lock_guard<std::mutex> lock(queue_mu_);
    stop_ = true;
    queue_cv_.notify_all();
  }
  if (worker_.joinable()) worker_.join();
}

bool WalBatchPipeline::ShouldFlushLocked(int64_t now_us) const {
  if (queue_.empty()) return false;
  if (queue_.size() >= config_.max_batch_size) return true;
  if (wal_->UnflushedBytes() >= config_.wal_batch_bytes) return true;
  if (batch_start_us_ == 0) return false;
  const bool timed_out =
      now_us >= batch_start_us_ + static_cast<int64_t>(config_.max_wait_us);
  if (!timed_out) return false;
  if (queue_.size() >= kMinThroughputQueue) return true;
  return queue_.size() >= kMinSparseQueue && queue_.size() < kMinThroughputQueue;
}

void WalBatchPipeline::CompleteJobs(const std::vector<std::shared_ptr<Job>>& jobs,
                                    const Status& fs, const Status& batch_status,
                                    EngineStats* stats, uint64_t waiters) {
  if (fs.ok() && stats && waiters > 0) {
    stats->fsync_batch_total++;
    stats->fsync_waiter_total += waiters;
    if (stats->fsync_batch_total > 0) {
      stats->fsync_merge_ratio =
          stats->fsync_waiter_total / stats->fsync_batch_total;
    }
  }
  const Status job_status = batch_status.ok() ? fs : batch_status;
  for (const auto& job : jobs) {
    if (!job_status.ok()) job->append_status = job_status;
    std::lock_guard<std::mutex> jlock(job->mu);
    job->durable = true;
    job->cv.notify_one();
  }
}

void WalBatchPipeline::DrainAndFlush(EngineStats* stats,
                                     bool commit_lock_held_by_caller) {
  std::vector<std::shared_ptr<Job>> jobs;
  jobs.reserve(config_.max_batch_size);
  std::vector<WalWriter::BatchItem> batch;
  batch.reserve(config_.max_batch_size);

  {
    std::lock_guard<std::mutex> lock(queue_mu_);
    while (!queue_.empty() && jobs.size() < config_.max_batch_size) {
      jobs.push_back(queue_.front());
      queue_.pop_front();
      if (wal_->UnflushedBytes() >= config_.wal_batch_bytes &&
          jobs.size() >= kMinThroughputQueue) {
        break;
      }
    }
  }

  if (jobs.empty()) return;

  batch.resize(jobs.size());
  for (size_t i = 0; i < jobs.size(); ++i) {
    batch[i].op = jobs[i]->op;
    batch[i].key = &jobs[i]->key;
    batch[i].value = &jobs[i]->value;
    batch[i].out_lsn = &jobs[i]->assigned_lsn;
  }

  (void)wal_->AppendMany(&batch);

  bool append_ok = true;
  for (size_t i = 0; i < jobs.size(); ++i) {
    jobs[i]->append_status = batch[i].status;
    if (!batch[i].status.ok()) append_ok = false;
  }
  if (!append_ok) {
    for (const auto& job : jobs) {
      std::lock_guard<std::mutex> jlock(job->mu);
      job->durable = true;
      job->cv.notify_one();
    }
    return;
  }

  const Status fs = wal_->Fsync();
  Status completion = fs;
  if (fs.ok() && commit_hook_) {
    std::vector<WalBatchCommitItem> items;
    items.reserve(jobs.size());
    for (const auto& job : jobs) {
      WalBatchCommitItem item{};
      item.op = job->op;
      item.key = &job->key;
      item.value = &job->value;
      item.lsn = job->assigned_lsn;
      items.push_back(item);
    }
    const Status apply = commit_hook_(items, stats, commit_lock_held_by_caller);
    if (!apply.ok()) completion = apply;
  }
  CompleteJobs(jobs, fs, completion, stats, jobs.size());
}

void WalBatchPipeline::WorkerLoop() {
  while (true) {
    EngineStats* stats = stats_target_.load();
    int64_t now_us = NowUs();

    {
      std::unique_lock<std::mutex> lock(queue_mu_);
      const bool ready_to_flush =
          !queue_.empty() && ShouldFlushLocked(now_us);
      if (!ready_to_flush) {
        queue_cv_.wait_for(lock, std::chrono::microseconds(20), [this] {
          return stop_ || !queue_.empty();
        });
      }
      if (stop_ && queue_.empty()) break;

      if (queue_.empty()) {
        batch_start_us_ = 0;
        continue;
      }
      now_us = NowUs();
      if (batch_start_us_ == 0) batch_start_us_ = now_us;

      if (!ShouldFlushLocked(now_us)) continue;
    }

    DrainAndFlush(stats, false);
    batch_start_us_ = 0;

    for (int burst = 0; burst < kMaxBurstDrains; ++burst) {
      {
        std::lock_guard<std::mutex> lock(queue_mu_);
        if (queue_.empty()) break;
        if (queue_.size() < kMinThroughputQueue &&
            wal_->UnflushedBytes() < config_.wal_batch_bytes) {
          break;
        }
      }
      DrainAndFlush(stats, false);
    }
  }
}

Status WalBatchPipeline::Put(const std::string& key, const std::string& value,
                             uint64_t* lsn, EngineStats* stats) {
  auto job = std::make_shared<Job>();
  job->op = WalOp::kPut;
  job->key = key;
  job->value = value;
  stats_target_.store(stats);
  {
    std::lock_guard<std::mutex> lock(queue_mu_);
    if (stop_) return Status::Internal("wal pipeline stopped");
    queue_.push_back(job);
    queue_cv_.notify_one();
  }
  std::unique_lock<std::mutex> lock(job->mu);
  job->cv.wait(lock, [&] { return job->durable; });
  if (!job->append_status.ok()) return job->append_status;
  if (lsn) *lsn = job->assigned_lsn;
  if (stats) stats->wal_append_total++;
  return Status::Ok();
}

Status WalBatchPipeline::Delete(const std::string& key, uint64_t* lsn,
                                EngineStats* stats) {
  auto job = std::make_shared<Job>();
  job->op = WalOp::kDelete;
  job->key = key;
  stats_target_.store(stats);
  {
    std::lock_guard<std::mutex> lock(queue_mu_);
    if (stop_) return Status::Internal("wal pipeline stopped");
    queue_.push_back(job);
    queue_cv_.notify_one();
  }
  std::unique_lock<std::mutex> lock(job->mu);
  job->cv.wait(lock, [&] { return job->durable; });
  if (!job->append_status.ok()) return job->append_status;
  if (lsn) *lsn = job->assigned_lsn;
  if (stats) stats->wal_append_total++;
  return Status::Ok();
}

Status WalBatchPipeline::FlushPending(EngineStats* stats) {
  for (int i = 0; i < 4000; ++i) {
    bool drain_queue = false;
    {
      std::lock_guard<std::mutex> lock(queue_mu_);
      if (queue_.empty() && wal_->UnflushedBytes() == 0) return Status::Ok();
      if (!queue_.empty()) {
        batch_start_us_ = NowUs() - static_cast<int64_t>(config_.max_wait_us);
        drain_queue = true;
      }
    }
    if (drain_queue) {
      DrainAndFlush(stats, true);
    } else if (wal_->UnflushedBytes() > 0) {
      const Status fs = wal_->Fsync();
      if (!fs.ok()) return fs;
    }
    {
      std::lock_guard<std::mutex> lock(queue_mu_);
      if (queue_.empty() && wal_->UnflushedBytes() == 0) return Status::Ok();
      if (!queue_.empty()) queue_cv_.notify_all();
    }
    if (drain_queue) {
      std::this_thread::sleep_for(std::chrono::microseconds(20));
    }
  }
  return Status::Ok();
}

}  // namespace ebtree
