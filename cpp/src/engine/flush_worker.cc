#include "ebtree/engine/flush_worker.h"

#include "ebtree/engine/shard_engine.h"

#include <chrono>

namespace ebtree {

BackgroundFlushWorker::BackgroundFlushWorker(ShardEngine* shard) : shard_(shard) {}

BackgroundFlushWorker::~BackgroundFlushWorker() { Stop(); }

void BackgroundFlushWorker::Start() {
  if (running_.exchange(true)) return;
  stop_ = false;
  thread_ = std::thread([this] { Run(); });
}

void BackgroundFlushWorker::Stop() {
  if (!running_) return;
  stop_ = true;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  running_ = false;
}

void BackgroundFlushWorker::Notify() { cv_.notify_one(); }

void BackgroundFlushWorker::Run() {
  while (!stop_) {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait_for(lock, std::chrono::milliseconds(50));
    if (stop_) break;
    if (!shard_) continue;
    lock.unlock();
    if (!shard_) continue;
    if (shard_->snapshot_pin_count() > 0) continue;
    const size_t count = shard_->memtable()->Snapshot().size();
    if (count < shard_->options().memtable_flush_threshold_keys) continue;
    (void)shard_->TryFlushBackground();
  }
}

}  // namespace ebtree
