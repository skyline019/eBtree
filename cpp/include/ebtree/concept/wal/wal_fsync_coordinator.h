#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "ebtree/common/config.h"
#include "ebtree/common/status.h"
#include "ebtree/concept/wal/wal.h"

namespace ebtree {

struct WalFsyncConfig {
  uint32_t max_batch_size{1};
  uint32_t max_wait_us{0};
  uint32_t wal_batch_bytes{4096};
};

class WalFsyncCoordinator {
 public:
  WalFsyncCoordinator(WalWriter* wal, WalFsyncConfig config);
  ~WalFsyncCoordinator();

  WalFsyncCoordinator(const WalFsyncCoordinator&) = delete;
  WalFsyncCoordinator& operator=(const WalFsyncCoordinator&) = delete;

  Status Await(uint64_t lsn, EngineStats* stats);
  Status FlushPending(EngineStats* stats);

 private:
  Status RunFsyncLocked(EngineStats* stats);
  bool ShouldFlushLocked() const;
  void FlusherLoop();
  void WakeFlusherLocked();

  WalWriter* wal_;
  WalFsyncConfig config_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::condition_variable flusher_cv_;
  uint64_t flushed_lsn_{0};
  uint64_t max_pending_lsn_{0};
  uint64_t pending_count_{0};
  bool fsync_running_{false};
  int64_t batch_deadline_us_{0};
  bool batch_deadline_active_{false};
  bool micro_batch_{false};
  bool stop_{false};
  std::thread flusher_;
  std::atomic<EngineStats*> stats_target_{nullptr};
};

}  // namespace ebtree
