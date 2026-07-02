#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"
#include "ebtree/engine/engine_attest.h"
#include "rar_chain.h"
#include "rar_merkle.h"
#include "rar_types.h"

namespace ebtree {
namespace audit {

struct RarChainSnapshotEvent {
  AttestExportReportV2 snapshot{};
  RarVerdict verdict{RarVerdict::kPass};
  std::string reason;
  uint64_t sequence{0};
};

using RarChainSnapshotCallback =
    std::function<void(const RarChainSnapshotEvent& event)>;

struct RarChainWorkerOptions {
  std::string chain_path;
  std::string op_log_path;
  size_t max_queue_depth{64};
  bool enabled{true};
  RarPolicy runtime_policy{};
  uint64_t rotate_max_entries{10000};
  RarChainSnapshotCallback on_snapshot;
};

class RarChainWorker {
 public:
  RarChainWorker() = default;
  ~RarChainWorker() { Stop(); }

  void Start(const RarChainWorkerOptions& opts);
  void Stop();

  void Enqueue(Engine* engine, uint64_t checkpoint_lsn);

  bool running() const { return running_.load(); }
  size_t pending_jobs() const;

 private:
  struct Job {
    Engine* engine{nullptr};
    uint64_t checkpoint_lsn{0};
  };

  void WorkerLoop();
  Status ProcessJob(const Job& job);
  void MaybeSignEntry(RarChainEntry* entry);

  RarChainWorkerOptions opts_{};
  std::thread worker_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Job> queue_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  CarlMerkleAccumulator merkle_{8};
};

void InstallRarChainWorker(Engine* engine, RarChainWorker* worker);

}  // namespace audit
}  // namespace ebtree
