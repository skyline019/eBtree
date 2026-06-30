#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ebtree/engine/engine.h"
#include "ebtree/engine/shard_router.h"

namespace {

std::vector<std::string> SampleKeys(uint32_t shard_count, int count) {
  std::vector<std::string> keys;
  keys.reserve(static_cast<size_t>(count));
  for (int i = 0; static_cast<int>(keys.size()) < count; ++i) {
    keys.push_back("sk" + std::to_string(i));
  }
  return keys;
}

}  // namespace

int main(int argc, char** argv) {
  uint32_t shard_count = 16;
  bool lazy = false;
  if (argc > 1) shard_count = static_cast<uint32_t>(std::stoul(argv[1]));
  if (argc > 2) lazy = std::string(argv[2]) == "lazy";
  if (!ebtree::ValidateShardCount(shard_count).ok()) {
    std::fprintf(stderr, "shard_count must be 1, 4, 16, or 256\n");
    return 1;
  }

  const auto dir =
      (std::filesystem::temp_directory_path() / "ebtree_bench_mscan").string();
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);

  ebtree::EngineOptions opts =
      ebtree::EngineOptions::BenchmarkGroupDefaults(dir);
  opts.shard_count = shard_count;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.sync_on_commit = true;

  {
    std::unique_ptr<ebtree::Engine> engine;
    if (!ebtree::Engine::Open(opts, &engine).ok()) {
      std::fprintf(stderr, "open failed\n");
      return 1;
    }
    const auto keys = SampleKeys(shard_count, 10000);
    for (const auto& key : keys) {
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

  opts.lazy_committed_load = lazy;
  std::unique_ptr<ebtree::Engine> engine;
  if (!ebtree::Engine::Open(opts, &engine).ok()) {
    std::fprintf(stderr, "reopen failed\n");
    return 1;
  }

  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "sk0";
  plan.range_end = "sk9999";
  plan.snapshot_lsn = engine->stable_lsn();

  constexpr int kScans = 20;
  std::vector<int64_t> latencies_ns;
  latencies_ns.reserve(kScans);
  size_t rows = 0;
  for (int r = 0; r < kScans; ++r) {
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
  std::printf(
      "multishard_scan_bench shards=%u lazy=%d rows=%zu p50_ms=%.3f p99_ms=%.3f\n",
      shard_count, lazy ? 1 : 0, rows, static_cast<double>(p50) / 1e6,
      static_cast<double>(p99) / 1e6);
  return 0;
}
