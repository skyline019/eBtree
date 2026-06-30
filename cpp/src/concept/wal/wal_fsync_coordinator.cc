#include "ebtree/concept/wal/wal_fsync_coordinator.h"

#include <algorithm>
#include <thread>

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
struct HighResClock {
  HighResClock() {
    if (!active_) {
      timeBeginPeriod(1);
      active_ = true;
    }
    QueryPerformanceFrequency(&freq_);
  }

  int64_t NowUs() const {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return (counter.QuadPart * 1000000) / freq_.QuadPart;
  }

  static inline bool active_{false};
  LARGE_INTEGER freq_{};
};

HighResClock g_hires;
#endif

int64_t BatchDeadlineUs(uint32_t max_wait_us) {
#if defined(_WIN32)
  return g_hires.NowUs() + static_cast<int64_t>(max_wait_us);
#else
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
             .count() +
         static_cast<int64_t>(max_wait_us);
#endif
}

int64_t NowUs() {
#if defined(_WIN32)
  return g_hires.NowUs();
#else
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
             .count();
#endif
}

constexpr int kCoalesceSpinUs = 250;

}  // namespace

WalFsyncCoordinator::WalFsyncCoordinator(WalWriter* wal, WalFsyncConfig config)
    : wal_(wal), config_(config) {
  if (config_.max_batch_size == 0) config_.max_batch_size = 1;
  if (config_.wal_batch_bytes == 0) config_.wal_batch_bytes = 4096;
  micro_batch_ = config_.max_batch_size > 1 && config_.max_wait_us > 0;
  if (micro_batch_) {
    flusher_ = std::thread([this] { FlusherLoop(); });
  }
}

WalFsyncCoordinator::~WalFsyncCoordinator() {
  if (micro_batch_) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      stop_ = true;
      flusher_cv_.notify_all();
      cv_.notify_all();
    }
    if (flusher_.joinable()) flusher_.join();
  }
}

void WalFsyncCoordinator::WakeFlusherLocked() { flusher_cv_.notify_one(); }

Status WalFsyncCoordinator::RunFsyncLocked(EngineStats* stats) {
  if (pending_count_ == 0 && wal_->UnflushedBytes() == 0) return Status::Ok();
  fsync_running_ = true;
  const uint64_t target = max_pending_lsn_;
  const uint64_t waiters = pending_count_;
  pending_count_ = 0;
  batch_deadline_active_ = false;
  mu_.unlock();
  const Status st = wal_->Fsync();
  mu_.lock();
  fsync_running_ = false;
  if (!st.ok()) {
    pending_count_ += waiters;
    cv_.notify_all();
    flusher_cv_.notify_all();
    return st;
  }
  flushed_lsn_ = std::max(flushed_lsn_, target);
  EngineStats* const stats_ptr = stats ? stats : stats_target_.load();
  if (stats_ptr) {
    stats_ptr->fsync_batch_total++;
    stats_ptr->fsync_waiter_total += waiters;
    if (stats_ptr->fsync_batch_total > 0) {
      stats_ptr->fsync_merge_ratio =
          stats_ptr->fsync_waiter_total / stats_ptr->fsync_batch_total;
    }
  }
  cv_.notify_all();
  return Status::Ok();
}

bool WalFsyncCoordinator::ShouldFlushLocked() const {
  if (pending_count_ == 0) return false;
  const bool batch_full = pending_count_ >= config_.max_batch_size;
  const bool immediate = config_.max_batch_size <= 1;
  const bool timed_out =
      batch_deadline_active_ && NowUs() >= batch_deadline_us_;
  const size_t staging = wal_->UnflushedBytes();
  const bool staging_full =
      config_.wal_batch_bytes > 0 && staging >= config_.wal_batch_bytes;
  if (batch_full || immediate || staging_full) return true;
  if (timed_out) return true;
  return false;
}

void WalFsyncCoordinator::FlusherLoop() {
  while (true) {
    std::unique_lock<std::mutex> lock(mu_);
    if (stop_) break;

    int64_t sleep_us = 100;
    if (batch_deadline_active_) {
      sleep_us = std::max<int64_t>(0, batch_deadline_us_ - NowUs());
      sleep_us = std::min<int64_t>(sleep_us, 200);
    }

    flusher_cv_.wait_for(lock, std::chrono::microseconds(sleep_us), [this] {
      return stop_ || ShouldFlushLocked();
    });
    if (stop_) break;

    if (ShouldFlushLocked() && !fsync_running_) {
      const Status st = RunFsyncLocked(stats_target_.load());
      if (!st.ok()) cv_.notify_all();
    }
  }
}

Status WalFsyncCoordinator::Await(uint64_t lsn, EngineStats* stats) {
  stats_target_.store(stats);
  std::unique_lock<std::mutex> lock(mu_);
  if (lsn <= flushed_lsn_) return Status::Ok();

  max_pending_lsn_ = std::max(max_pending_lsn_, lsn);
  ++pending_count_;

  if (pending_count_ == 1 && config_.max_batch_size > 1 &&
      config_.max_wait_us > 0) {
    const int64_t coalesce_until = NowUs() + kCoalesceSpinUs;
    while (NowUs() < coalesce_until) {
      if (pending_count_ >= 2 ||
          wal_->UnflushedBytes() >= config_.wal_batch_bytes) {
        break;
      }
      lock.unlock();
#if defined(_WIN32)
      YieldProcessor();
#else
      std::this_thread::yield();
#endif
      lock.lock();
      if (lsn <= flushed_lsn_) return Status::Ok();
    }
    batch_deadline_us_ = BatchDeadlineUs(config_.max_wait_us);
    batch_deadline_active_ = true;
  } else if (!batch_deadline_active_ && config_.max_batch_size > 1 &&
             config_.max_wait_us > 0) {
    batch_deadline_us_ = BatchDeadlineUs(config_.max_wait_us);
    batch_deadline_active_ = true;
  }

  if (ShouldFlushLocked() && !fsync_running_) {
    const Status st = RunFsyncLocked(stats);
    if (!st.ok()) return st;
    if (lsn <= flushed_lsn_) return Status::Ok();
  }
  if (micro_batch_) WakeFlusherLocked();

  while (lsn > flushed_lsn_) {
    if (fsync_running_) {
      cv_.wait(lock);
      continue;
    }
    if (ShouldFlushLocked()) {
      const Status st = RunFsyncLocked(stats);
      if (!st.ok()) return st;
      continue;
    }
    if (micro_batch_) {
      WakeFlusherLocked();
      cv_.wait_for(lock, std::chrono::microseconds(50));
    } else if (batch_deadline_active_) {
      const int64_t remaining_us = batch_deadline_us_ - NowUs();
      if (remaining_us <= 0) continue;
      cv_.wait_for(lock, std::chrono::microseconds(
                              std::min<int64_t>(remaining_us, 200)));
    } else {
      cv_.wait(lock);
    }
  }
  return Status::Ok();
}

Status WalFsyncCoordinator::FlushPending(EngineStats* stats) {
  std::unique_lock<std::mutex> lock(mu_);
  while (pending_count_ > 0 || wal_->UnflushedBytes() > 0 ||
         max_pending_lsn_ > flushed_lsn_) {
    if (fsync_running_) {
      cv_.wait(lock);
      continue;
    }
    if (pending_count_ == 0 && wal_->UnflushedBytes() == 0) break;
    const Status st = RunFsyncLocked(stats);
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

}  // namespace ebtree
