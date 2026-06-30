#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace ebtree {

class ShardEngine;

class BackgroundSummaryValidator {
 public:
  explicit BackgroundSummaryValidator(ShardEngine* shard);
  ~BackgroundSummaryValidator();

  void Start();
  void Stop();
  void WaitUntilIdle();

 private:
  void Run();

  ShardEngine* shard_{nullptr};
  std::thread thread_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> idle_{true};
};

}  // namespace ebtree
