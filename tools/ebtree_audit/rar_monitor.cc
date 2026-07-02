#include "rar_monitor.h"

#include "policy_gate.h"
#include "rar_chain.h"
#include "rar_chain_anchor.h"

#include <filesystem>
#include <iostream>

namespace ebtree {
namespace audit {

namespace {

bool RarLogEnabled() {
  const char* log = std::getenv("EBTREE_RAR_LOG");
  return log && (std::string(log) == "warn" || std::string(log) == "1");
}

}  // namespace

void RarMonitor::OnChainSnapshot(const AttestExportReportV2& snapshot,
                                 RarVerdict verdict, const std::string& reason,
                                 uint64_t sequence) {
  (void)snapshot;
  {
    std::lock_guard<std::mutex> lock(mu_);
    last_chain_verdict_ = verdict;
    last_chain_reason_ = reason;
    last_chain_sequence_ = sequence;
  }
  RefreshRuntimeState();
}

void RarMonitor::WarnChainDrop(uint64_t new_total) {
  if (new_total <= last_seen_drop_total_) return;
  last_seen_drop_total_ = new_total;
  if (RarLogEnabled()) {
    std::cerr << "rar chain: drop_total increased to " << new_total << "\n";
  }
  if (opts_.reject_on_chain_drop) {
    chain_drop_rejected_ = true;
  }
}

void RarMonitor::StartStartupVerify() {
  if (opts_.chain_path.empty()) return;
  stop_requested_.store(false);
  startup_verify_ = std::thread([this]() {
    RarChainVerifyReport report{};
    const Status st = VerifyRarChain(opts_.chain_path, &report);
  if (!st.ok()) {
      std::lock_guard<std::mutex> lock(mu_);
      startup_chain_consistent_ = false;
      return;
    }
    std::lock_guard<std::mutex> lock(mu_);
    startup_chain_consistent_ = report.consistent;
    if (!report.consistent && !report.errors.empty()) {
      last_chain_reason_ = report.errors.front();
    }
  });
}

void RarMonitor::Install(Engine* engine, const RarMonitorOptions& opts) {
  Stop();
  if (!engine || !opts.enabled) return;
  engine_ = engine;
  opts_ = opts;
  chain_drop_rejected_ = false;
  last_seen_drop_total_ = engine_->stats().rar_chain_drop_total;

  RarChainWorkerOptions worker_opts{};
  worker_opts.chain_path = opts.chain_path;
  worker_opts.op_log_path = opts.op_log_path;
  worker_opts.enabled = opts.enabled;
  worker_opts.max_queue_depth = opts.max_queue_depth;
  worker_opts.runtime_policy = opts.runtime_policy;
  worker_opts.on_snapshot = [this](const RarChainSnapshotEvent& event) {
    OnChainSnapshot(event.snapshot, event.verdict, event.reason,
                    event.sequence);
  };

  worker_.Start(worker_opts);
  InstallRarChainWorker(engine, &worker_);
  if (opts.write_circuit) {
    engine->SetWriteGuard([this]() {
      RefreshRuntimeState();
      return AllowsWrite()
                 ? Status::Ok()
                 : Status::Corrupt("rar monitor: write circuit open");
    });
  }
  RefreshRuntimeState();
  StartStartupVerify();
}

void RarMonitor::Stop() {
  stop_requested_.store(true);
  if (engine_) {
    engine_->SetWriteGuard({});
  }
  worker_.Stop();
  if (startup_verify_.joinable()) startup_verify_.join();
  engine_ = nullptr;
}

bool RarMonitor::AllowsWrite() const {
  std::lock_guard<std::mutex> lock(mu_);
  return allows_write_;
}

void RarMonitor::RefreshRuntimeState() {
  if (!engine_) return;

  const EngineStats stats = engine_->stats();
  WarnChainDrop(stats.rar_chain_drop_total);

  bool stats_ok = true;
  if (opts_.write_circuit) {
    if (opts_.runtime_policy.require_unexpected_path_zero &&
        stats.unexpected_path_total > 0) {
      stats_ok = false;
    }
    if (stats.decompress_fail_total > opts_.runtime_policy.max_decompress_fail) {
      stats_ok = false;
    }
  }

  std::lock_guard<std::mutex> lock(mu_);
  bool chain_ok = true;
  if (opts_.write_circuit) {
    chain_ok = last_chain_verdict_ == RarVerdict::kPass;
  }
  allows_write_ = stats_ok && chain_ok && !chain_drop_rejected_;
}

RarStatusSnapshot RarMonitor::StatusSnapshot() const {
  RarStatusSnapshot out{};
  if (!engine_) return out;

  const EngineStats stats = engine_->stats();
  std::lock_guard<std::mutex> lock(mu_);
  out.allows_write = allows_write_;
  out.unexpected_path_total = stats.unexpected_path_total;
  out.decompress_fail_total = stats.decompress_fail_total;
  out.rar_chain_drop_total = stats.rar_chain_drop_total;
  out.last_chain_sequence = last_chain_sequence_;
  out.last_chain_verdict = RarVerdictToString(last_chain_verdict_);
  out.last_chain_reason = last_chain_reason_;
  out.startup_chain_consistent = startup_chain_consistent_;
  out.worker_running = worker_.running();
  if (!opts_.chain_path.empty()) {
    const std::string engine_path =
        std::filesystem::path(opts_.chain_path).parent_path().string();
    const std::string anchor_dir = DefaultCarlAnchorDir(engine_path);
    CarlSignedTreeHead anchor{};
    bool anchor_found = false;
    if (LoadLatestCarlAnchor(
            anchor_dir,
            std::filesystem::path(opts_.chain_path).filename().string(),
            &anchor, &anchor_found)
            .ok() &&
        anchor_found) {
      out.last_anchor_sequence = anchor.chain_sequence;
      out.last_anchor_hash = anchor.root_hash;
    }
  }
  return out;
}

void InstallRarMonitor(Engine* engine, const RarMonitorOptions& opts,
                       RarMonitor* monitor) {
  if (!engine || !monitor) return;
  monitor->Install(engine, opts);
}

Status OpenWithRarMonitor(const EngineOptions& engine_opts,
                          const RarMonitorOptions& rar_opts,
                          std::unique_ptr<Engine>* engine_out,
                          RarMonitor* monitor_out) {
  if (!engine_out) return Status::InvalidArgument("engine_out is null");
  std::unique_ptr<Engine> engine;
  const Status os = Engine::Open(engine_opts, &engine);
  if (!os.ok()) return os;
  if (monitor_out && rar_opts.enabled) {
    monitor_out->Install(engine.get(), rar_opts);
  }
  *engine_out = std::move(engine);
  return Status::Ok();
}

}  // namespace audit
}  // namespace ebtree
