#include "carl_eval_common.h"

#include "digest.h"
#include "rar_chain.h"
#include "rar_chain_anchor.h"
#include "rar_monitor.h"
#include "rar_snapshot_builder.h"

#include "ebtree/engine/engine_attest.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>

namespace ebtree {
namespace bench {
namespace carl_eval {

namespace {

int ThreadCount() {
  const unsigned hw = std::thread::hardware_concurrency();
  int kThreads = hw == 0 ? 128 : static_cast<int>(hw) * 16;
  if (kThreads < 64) kThreads = 64;
  if (kThreads > 128) kThreads = 128;
  return kThreads;
}

Write100kResult RunWrite100kImpl(const std::string& dir, bool with_carl) {
  Write100kResult out{};
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  const int kThreads = ThreadCount();
  const int kTotalOps = 100000;
  const int kPerThread = (kTotalOps + kThreads - 1) / kThreads;

  std::unique_ptr<ebtree::Engine> engine;
  audit::RarMonitor monitor;
  if (with_carl) {
    audit::RarMonitorOptions rar_opts{};
    rar_opts.enabled = true;
    rar_opts.chain_path = dir + "/ebtree.rar.chain.jsonl";
    rar_opts.write_circuit = true;
    rar_opts.runtime_policy.require_unexpected_path_zero = true;
    if (!audit::OpenWithRarMonitor(opts, rar_opts, &engine, &monitor).ok()) {
      return out;
    }
  } else if (!ebtree::Engine::Open(opts, &engine).ok()) {
    return out;
  }

  std::vector<std::thread> pool;
  pool.reserve(kThreads);
  std::atomic<int> put_failures{0};
  const auto start = std::chrono::steady_clock::now();
  for (int t = 0; t < kThreads; ++t) {
    pool.emplace_back([engine = engine.get(), t, kPerThread, &put_failures]() {
      for (int i = 0; i < kPerThread; ++i) {
        if (!engine->Put("bk" + std::to_string(t) + "_" + std::to_string(i), "v")
                 .ok()) {
          put_failures.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& th : pool) th.join();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  if (put_failures.load() != 0 || elapsed.count() <= 0) return out;
  out.elapsed_ms = elapsed.count();
  out.ops_per_sec =
      static_cast<double>(kPerThread * kThreads) /
      (static_cast<double>(elapsed.count()) / 1000.0);
  return out;
}

}  // namespace

Write100kResult RunNoCarlWrite100k(const std::string& dir) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  return RunWrite100kImpl(dir, false);
}

Write100kResult RunCarlMonitorWrite100k(const std::string& dir) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  return RunWrite100kImpl(dir, true);
}

VerifyLatencyResult MeasureChainVerifyLatency(const std::string& chain_path,
                                              uint64_t entry_count) {
  VerifyLatencyResult out{};
  AttestExportReportV2 kernel{};
  kernel.checkpoint_lsn = 1;
  std::string prev;
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(chain_path).parent_path(), ec);
  std::filesystem::remove(chain_path, ec);
  for (uint64_t seq = 1; seq <= entry_count; ++seq) {
    const std::string body = audit::BuildChainBodyJson(
        seq, seq, prev, "", static_cast<int64_t>(seq), kernel);
    audit::RarChainEntry entry{};
    entry.sequence = seq;
    entry.body_json = body;
    entry.rar_sha256 = audit::Sha256HexString(body);
    entry.prev_rar_sha256 = prev;
    if (!audit::AppendRarChainEntry(chain_path, entry).ok()) return out;
    prev = entry.rar_sha256;
  }
  out.entry_count = entry_count;
  const auto start = std::chrono::steady_clock::now();
  audit::RarChainVerifyReport report{};
  if (!audit::VerifyRarChain(chain_path, &report).ok() || !report.consistent) {
    return VerifyLatencyResult{};
  }
  out.verify_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count();
  return out;
}

AnchorLatencyResult MeasureAnchorPublishLatency(const std::string& chain_path,
                                                const std::string& anchor_dir) {
  AnchorLatencyResult out{};
  AttestExportReportV2 kernel{};
  const std::string body = audit::BuildChainBodyJson(1, 1, "", "", 1, kernel);
  audit::RarChainEntry entry{};
  entry.body_json = body;
  entry.rar_sha256 = audit::Sha256HexString(body);
  entry.sequence = 1;
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(chain_path).parent_path(), ec);
  std::filesystem::create_directories(anchor_dir, ec);
  std::filesystem::remove(chain_path, ec);
  if (!audit::AppendRarChainEntry(chain_path, entry).ok()) return out;
  const auto start = std::chrono::steady_clock::now();
  audit::CarlSignedTreeHead sth{};
  if (!audit::PublishCarlAnchor(chain_path, anchor_dir, &sth).ok()) return out;
  out.publish_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count();
  return out;
}

LazyScanResult MeasureLazyScan10k(const std::string& dir) {
  LazyScanResult out{};
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  opts.background_summary_validate = false;
  opts.background_flush = false;
  {
    std::unique_ptr<Engine> engine;
    if (!Engine::Open(opts, &engine).ok()) return out;
    for (int i = 0; i < 10000; ++i) {
      if (!engine->Put("lsk" + std::to_string(i), "lsv").ok()) return out;
    }
    if (!engine->Checkpoint().ok()) return out;
  }
  std::unique_ptr<Engine> engine;
  if (!Engine::Open(opts, &engine).ok()) return out;
  TypedPlan plan{};
  plan.op = PredicateOp::kRange;
  plan.key = "lsk0";
  plan.range_end = "lsk9999";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  const auto start = std::chrono::steady_clock::now();
  if (!engine->Scan(plan, &rows).ok()) return out;
  out.scan_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count();
  out.row_count = rows.size();
  return out;
}

void PrintEvalMarkdown(const Write100kResult& no_carl,
                       const Write100kResult& carl,
                       const VerifyLatencyResult& verify_1k,
                       const AnchorLatencyResult& anchor,
                       const LazyScanResult& lazy_scan) {
  const double ratio =
      no_carl.ops_per_sec > 0 ? carl.ops_per_sec / no_carl.ops_per_sec : 0.0;
  std::printf("# CARL Eval Results\n\n");
  std::printf("Generated: Release bench on local NVMe (single run). ");
  std::printf("Gate enforces CARL/no-CARL ratio >= 0.99.\n\n");
  std::printf("## Workload-A-equivalent write (100k Put, kBalanced)\n\n");
  std::printf("| Config | 100k Put TPS | Elapsed ms | Notes |\n");
  std::printf("|--------|-------------|------------|-------|\n");
  std::printf("| no-CARL | %.0f | %lld | attestation off |\n",
              no_carl.ops_per_sec, static_cast<long long>(no_carl.elapsed_ms));
  std::printf("| CARL MONITOR | %.0f | %lld | ratio=%.3f |\n",
              carl.ops_per_sec, static_cast<long long>(carl.elapsed_ms), ratio);
  std::printf("\n| Metric | Value |\n");
  std::printf("|--------|-------|\n");
  std::printf("| chain verify %llu entries | %lld ms |\n",
              static_cast<unsigned long long>(verify_1k.entry_count),
              static_cast<long long>(verify_1k.verify_ms));
  std::printf("| anchor publish | %lld ms |\n",
              static_cast<long long>(anchor.publish_ms));
  std::printf("| lazy scan 10k rows | %lld ms (%llu rows) |\n",
              static_cast<long long>(lazy_scan.scan_ms),
              static_cast<unsigned long long>(lazy_scan.row_count));
  std::printf("| tamper detect | CarlAnchorTamperDetect gate (P15) |\n");
  std::printf("| write ratio gate | P16-carl-eval >= 0.99 |\n");
}

}  // namespace carl_eval
}  // namespace bench
}  // namespace ebtree
