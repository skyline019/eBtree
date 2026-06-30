#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ebtree/engine/engine.h"

namespace {

struct LatencyStats {
  int64_t p50_ns{0};
  int64_t p99_ns{0};
};

LatencyStats ComputeLatency(std::vector<int64_t> latencies) {
  LatencyStats out{};
  if (latencies.empty()) return out;
  std::sort(latencies.begin(), latencies.end());
  out.p50_ns = latencies[latencies.size() / 2];
  out.p99_ns = latencies[(latencies.size() * 99) / 100];
  return out;
}

double RunWrite(ebtree::Engine* engine, int ops) {
  const auto start = std::chrono::steady_clock::now();
  if (engine->durability() == ebtree::DurabilityClass::kBalanced) {
    constexpr int kThreads = 4;
    const int per_thread = ops / kThreads;
    std::vector<std::thread> pool;
    pool.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      pool.emplace_back([engine, t, per_thread]() {
        for (int i = 0; i < per_thread; ++i) {
          if (!engine->Put("mk" + std::to_string(t) + "_" + std::to_string(i), "v")
                   .ok()) {
            std::exit(1);
          }
        }
      });
    }
    for (auto& th : pool) th.join();
  } else {
    for (int i = 0; i < ops; ++i) {
      if (!engine->Put("mk" + std::to_string(i), "v").ok()) std::exit(1);
    }
    if (engine->durability() == ebtree::DurabilityClass::kGroup) {
      (void)engine->GroupCommit();
    }
  }
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);
  return static_cast<double>(ops) / (static_cast<double>(us.count()) / 1e6);
}

LatencyStats RunGets(ebtree::Engine* engine, int keys) {
  std::vector<int64_t> latencies;
  latencies.reserve(keys);
  for (int i = 0; i < keys; ++i) {
    std::string value;
    const auto start = std::chrono::steady_clock::now();
    if (!engine->Get("mk" + std::to_string(i), &value).ok()) std::exit(1);
    latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count());
  }
  return ComputeLatency(std::move(latencies));
}

double RunScanP50Ms(ebtree::Engine* engine, int keys, int scans) {
  std::vector<int64_t> latencies;
  latencies.reserve(scans);
  for (int r = 0; r < scans; ++r) {
    ebtree::TypedPlan plan;
    plan.op = ebtree::PredicateOp::kRange;
    plan.key = "mk0";
    plan.range_end = "mk" + std::to_string(keys - 1);
    plan.snapshot_lsn = engine->stable_lsn();
    std::vector<std::pair<std::string, std::string>> rows;
    const auto start = std::chrono::steady_clock::now();
    if (!engine->Scan(plan, &rows).ok()) std::exit(1);
    latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count());
  }
  const auto stats = ComputeLatency(std::move(latencies));
  return static_cast<double>(stats.p50_ns) / 1e6;
}

void SeedAndCheckpoint(const ebtree::EngineOptions& base_opts, int keys) {
  std::unique_ptr<ebtree::Engine> engine;
  if (!ebtree::Engine::Open(base_opts, &engine).ok()) std::exit(1);
  for (int i = 0; i < keys; ++i) {
    if (!engine->Put("mk" + std::to_string(i), "payload").ok()) std::exit(1);
  }
  if (!engine->Checkpoint().ok()) std::exit(1);
}

}  // namespace

int main() {
  constexpr int kKeys = 10000;
  constexpr int kWriteOps = 50000;
  constexpr int kScans = 10;

  const auto base_dir =
      (std::filesystem::temp_directory_path() / "ebtree_bench_matrix").string();
  std::filesystem::remove_all(base_dir);
  std::filesystem::create_directories(base_dir);

  struct Row {
    const char* durability;
    const char* read_mode;
    double write_ops;
    double read_p99_us;
    double scan_p50_ms;
    uint64_t fsync_merge_ratio;
  };
  std::vector<Row> matrix;

  enum class Tier { kSync, kBalanced, kGroup };
  for (Tier tier : {Tier::kSync, Tier::kBalanced, Tier::kGroup}) {
    for (bool lazy : {false, true}) {
      const char* tier_name =
          tier == Tier::kSync ? "kSync"
                              : (tier == Tier::kBalanced ? "kBalanced" : "kGroup");
      const std::string tag =
          std::string(tier_name) + "_" + (lazy ? "lazy" : "committed");
      const auto dir = base_dir + "/" + tag;
      std::filesystem::create_directories(dir);

      ebtree::EngineOptions opts =
          tier == Tier::kSync
              ? ebtree::EngineOptions::EnterpriseDefaults(dir)
              : (tier == Tier::kBalanced
                     ? ebtree::EngineOptions::ProductionDefaults(dir)
                     : ebtree::EngineOptions::BenchmarkGroupDefaults(dir));
      opts.lazy_committed_load = lazy;

      SeedAndCheckpoint(opts, kKeys);

      std::unique_ptr<ebtree::Engine> engine;
      if (!ebtree::Engine::Open(opts, &engine).ok()) std::exit(1);

      const double write_ops = RunWrite(engine.get(), kWriteOps);
      const auto read_stats = RunGets(engine.get(), kKeys);
      const double scan_p50 = RunScanP50Ms(engine.get(), kKeys, kScans);

      matrix.push_back({tier_name, lazy ? "lazy" : "committed", write_ops,
                        static_cast<double>(read_stats.p99_ns) / 1000.0,
                        scan_p50, engine->stats().fsync_merge_ratio});
    }
  }

  std::printf("perf_matrix_bench keys=%d write_ops=%d page_cache_prod=128\n",
              kKeys, kWriteOps);
  for (const auto& row : matrix) {
    std::printf(
        "matrix durability=%s read=%s write_ops_per_sec=%.0f read_p99_us=%.1f "
        "scan_p50_ms=%.3f fsync_merge_ratio=%llu\n",
        row.durability, row.read_mode, row.write_ops, row.read_p99_us,
        row.scan_p50_ms, static_cast<unsigned long long>(row.fsync_merge_ratio));
  }
  return 0;
}
