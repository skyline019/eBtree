#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ebtree/common/config.h"
#include "ebtree/common/status.h"
#include "ebtree/concept/wal/wal.h"
#include "ebtree/concept/wal/wal_fsync_coordinator.h"

namespace ebtree {

struct WalBatchCommitItem {
  WalOp op{WalOp::kPut};
  const std::string* key{nullptr};
  const std::string* value{nullptr};
  uint64_t lsn{0};
};

using WalBatchCommitHook = std::function<Status(
    const std::vector<WalBatchCommitItem>&, EngineStats*, bool lock_held)>;

class WalBatchPipeline {
 public:
  WalBatchPipeline(WalWriter* wal, WalFsyncConfig config);
  ~WalBatchPipeline();

  WalBatchPipeline(const WalBatchPipeline&) = delete;
  WalBatchPipeline& operator=(const WalBatchPipeline&) = delete;

  Status Put(const std::string& key, const std::string& value, uint64_t* lsn,
             EngineStats* stats);
  Status Delete(const std::string& key, uint64_t* lsn, EngineStats* stats);
  Status FlushPending(EngineStats* stats);

  void SetCommitHook(WalBatchCommitHook hook);

 private:
  struct Job {
    WalOp op{WalOp::kPut};
    std::string key;
    std::string value;
    uint64_t assigned_lsn{0};
    bool durable{false};
    Status append_status{Status::Ok()};
    std::mutex mu;
    std::condition_variable cv;
  };

  void WorkerLoop();
  void DrainAndFlush(EngineStats* stats, bool commit_lock_held_by_caller);
  bool ShouldFlushLocked(int64_t now_us) const;
  void CompleteJobs(const std::vector<std::shared_ptr<Job>>& jobs,
                    const Status& fs, const Status& batch_status,
                    EngineStats* stats, uint64_t waiters);

  WalWriter* wal_;
  WalFsyncConfig config_;
  std::mutex queue_mu_;
  std::condition_variable queue_cv_;
  std::deque<std::shared_ptr<Job>> queue_;
  int64_t batch_start_us_{0};
  bool stop_{false};
  std::thread worker_;
  std::atomic<EngineStats*> stats_target_{nullptr};
  WalBatchCommitHook commit_hook_;
};

}  // namespace ebtree
