#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ebtree/engine/engine.h"

int main() {
  const auto dir =
      (std::filesystem::temp_directory_path() / "ebtree_bench_scan").string();
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);

  ebtree::EngineOptions opts =
      ebtree::EngineOptions::ProductionDefaults(dir);

  {
    std::unique_ptr<ebtree::Engine> engine;
    if (!ebtree::Engine::Open(opts, &engine).ok()) {
      std::fprintf(stderr, "open failed\n");
      return 1;
    }
    constexpr int kKeys = 10000;
    for (int i = 0; i < kKeys; ++i) {
      const std::string key = "k" + std::to_string(i);
      if (!engine->Put(key, "payload").ok()) {
        std::fprintf(stderr, "put failed\n");
        return 1;
      }
    }
    if (!engine->Checkpoint().ok()) {
      std::fprintf(stderr, "checkpoint failed\n");
      return 1;
    }
  }

  std::unique_ptr<ebtree::Engine> engine;
  if (!ebtree::Engine::Open(opts, &engine).ok()) {
    std::fprintf(stderr, "reopen failed\n");
    return 1;
  }

  constexpr int kKeys = 10000;
  constexpr int kScans = 20;
  std::vector<int64_t> latencies_ns;
  latencies_ns.reserve(kScans);
  size_t rows = 0;
  for (int r = 0; r < kScans; ++r) {
    ebtree::TypedPlan plan;
    plan.op = ebtree::PredicateOp::kRange;
    plan.key = "k0";
    plan.range_end = "k9999";
    plan.snapshot_lsn = engine->stable_lsn();
    std::vector<std::pair<std::string, std::string>> out;
    const auto start = std::chrono::steady_clock::now();
    if (!engine->Scan(plan, &out).ok()) {
      std::fprintf(stderr, "scan failed\n");
      return 1;
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start);
    latencies_ns.push_back(ns.count());
    rows = out.size();
  }

  std::sort(latencies_ns.begin(), latencies_ns.end());
  const int64_t p50 = latencies_ns[latencies_ns.size() / 2];
  const int64_t p99 = latencies_ns[(latencies_ns.size() * 99) / 100];
  const double p50_ms = static_cast<double>(p50) / 1e6;
  const double keys_per_sec =
      rows > 0 ? static_cast<double>(rows) / (static_cast<double>(p50) / 1e9)
               : 0.0;
  std::printf("scan_bench durability=kSync page_cache=%zu keys=%d scans=%d rows=%zu p50_ms=%.3f p99_ms=%.3f "
              "keys_per_sec_p50=%.0f\n",
              opts.page_cache_capacity,
      kKeys, kScans, rows, p50_ms, static_cast<double>(p99) / 1e6,
      keys_per_sec);

  ebtree::EngineOptions lazy_opts = opts;
  lazy_opts.lazy_committed_load = true;
  engine.reset();
  if (!ebtree::Engine::Open(lazy_opts, &engine).ok()) {
    std::fprintf(stderr, "lazy reopen failed\n");
    return 1;
  }
  latencies_ns.clear();
  for (int r = 0; r < kScans; ++r) {
    ebtree::TypedPlan lazy_plan;
    lazy_plan.op = ebtree::PredicateOp::kRange;
    lazy_plan.key = "k0";
    lazy_plan.range_end = "k9999";
    lazy_plan.snapshot_lsn = engine->stable_lsn();
    std::vector<std::pair<std::string, std::string>> out;
    const auto start = std::chrono::steady_clock::now();
    if (!engine->Scan(lazy_plan, &out).ok()) {
      std::fprintf(stderr, "lazy scan failed\n");
      return 1;
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start);
    latencies_ns.push_back(ns.count());
    rows = out.size();
  }
  std::sort(latencies_ns.begin(), latencies_ns.end());
  const int64_t lazy_p50 = latencies_ns[latencies_ns.size() / 2];
  std::printf("scan_bench_lazy keys=%d rows=%zu p50_ms=%.3f\n", kKeys, rows,
              static_cast<double>(lazy_p50) / 1e6);
  return 0;
}
