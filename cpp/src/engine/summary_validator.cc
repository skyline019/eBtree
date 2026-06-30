#include "ebtree/engine/summary_validator.h"

#include "ebtree/engine/shard_engine.h"

#include <chrono>

namespace ebtree {

BackgroundSummaryValidator::BackgroundSummaryValidator(ShardEngine* shard)
    : shard_(shard) {}

BackgroundSummaryValidator::~BackgroundSummaryValidator() { Stop(); }

void BackgroundSummaryValidator::Start() {
  if (running_.exchange(true)) return;
  stop_ = false;
  idle_ = false;
  thread_ = std::thread([this] { Run(); });
}

void BackgroundSummaryValidator::Stop() {
  if (!running_) return;
  stop_ = true;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  running_ = false;
}

void BackgroundSummaryValidator::WaitUntilIdle() {
  std::unique_lock<std::mutex> lock(mu_);
  cv_.wait_for(lock, std::chrono::seconds(5),
               [this] { return idle_.load(); });
}

void BackgroundSummaryValidator::Run() {
  while (!stop_) {
    if (shard_) {
      (void)shard_->TryRepairSummaryIfDrifted();
    }
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait_for(lock, std::chrono::milliseconds(50),
                   [this] { return stop_.load(); });
    }
  }
  idle_ = true;
  cv_.notify_all();
}

}  // namespace ebtree
