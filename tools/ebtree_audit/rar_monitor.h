#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "ebtree/engine/engine.h"
#include "ebtree/engine/engine_attest.h"
#include "rar_chain_worker.h"
#include "rar_types.h"

namespace ebtree {
namespace audit {

struct RarMonitorOptions {
  bool enabled{true};
  std::string chain_path;
  std::string op_log_path;
  RarPolicy runtime_policy{};
  bool write_circuit{false};
  bool reject_on_chain_drop{false};
  size_t max_queue_depth{64};
};

struct RarStatusSnapshot {
  bool allows_write{true};
  uint64_t unexpected_path_total{0};
  uint64_t decompress_fail_total{0};
  uint64_t rar_chain_drop_total{0};
  uint64_t last_chain_sequence{0};
  uint64_t last_anchor_sequence{0};
  std::string last_anchor_hash;
  std::string last_chain_verdict{"PASS"};
  std::string last_chain_reason;
  bool startup_chain_consistent{true};
  bool worker_running{false};
};

class RarMonitor {
 public:
  RarMonitor() = default;
  ~RarMonitor() { Stop(); }

  void Install(Engine* engine, const RarMonitorOptions& opts);
  void Stop();

  bool AllowsWrite() const;
  void RefreshRuntimeState();

  RarStatusSnapshot StatusSnapshot() const;
  bool WorkerRunning() const { return worker_.running(); }

 private:
  void OnChainSnapshot(const AttestExportReportV2& snapshot, RarVerdict verdict,
                       const std::string& reason, uint64_t sequence);
  void StartStartupVerify();
  void WarnChainDrop(uint64_t new_total);

  Engine* engine_{nullptr};
  RarMonitorOptions opts_{};
  RarChainWorker worker_{};
  mutable std::mutex mu_;
  bool allows_write_{true};
  RarVerdict last_chain_verdict_{RarVerdict::kPass};
  std::string last_chain_reason_;
  uint64_t last_chain_sequence_{0};
  uint64_t last_seen_drop_total_{0};
  bool startup_chain_consistent_{true};
  bool chain_drop_rejected_{false};
  std::thread startup_verify_;
  std::atomic<bool> stop_requested_{false};
};

void InstallRarMonitor(Engine* engine, const RarMonitorOptions& opts,
                       RarMonitor* monitor);

Status OpenWithRarMonitor(const EngineOptions& engine_opts,
                          const RarMonitorOptions& rar_opts,
                          std::unique_ptr<Engine>* engine_out,
                          RarMonitor* monitor_out);

}  // namespace audit
}  // namespace ebtree
