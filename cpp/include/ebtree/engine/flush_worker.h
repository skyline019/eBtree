#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "ebtree/common/status.h"

namespace ebtree {

class ShardEngine;

class BackgroundFlushWorker {
 public:
  explicit BackgroundFlushWorker(ShardEngine* shard);
  ~BackgroundFlushWorker();

  void Start();
  void Stop();
  void Notify();

 private:
  void Run();

  ShardEngine* shard_{nullptr};
  std::thread thread_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> running_{false};
};

}  // namespace ebtree
