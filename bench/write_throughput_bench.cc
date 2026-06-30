#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ebtree/engine/engine.h"

namespace {

double RunWriteBench(ebtree::Engine* engine, int ops, const char* label) {
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < ops; ++i) {
    const std::string key = "k" + std::to_string(i);
    if (!engine->Put(key, "v").ok()) {
      std::fprintf(stderr, "%s put failed at %d\n", label, i);
      std::exit(1);
    }
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);
  const double sec = static_cast<double>(elapsed.count()) / 1e6;
  const double ops_per_sec = static_cast<double>(ops) / sec;
  std::printf("%s ops=%d elapsed_sec=%.3f ops_per_sec=%.0f\n", label, ops, sec,
              ops_per_sec);
  return ops_per_sec;
}

int BalancedConcurrentThreads() {
  const unsigned hw = std::thread::hardware_concurrency();
  if (hw == 0) return 128;
  const int scaled = static_cast<int>(hw) * 16;
  if (scaled < 64) return 64;
  if (scaled > 128) return 128;
  return scaled;
}

}  // namespace

int main() {
  constexpr int kOps = 100000;

  {
    const auto dir =
        (std::filesystem::temp_directory_path() / "ebtree_bench_write_kgroup")
            .string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    ebtree::EngineOptions opts =
        ebtree::EngineOptions::BenchmarkGroupDefaults(dir);
    std::unique_ptr<ebtree::Engine> engine;
    if (!ebtree::Engine::Open(opts, &engine).ok()) {
      std::fprintf(stderr, "kGroup open failed\n");
      return 1;
    }
    (void)RunWriteBench(engine.get(), kOps, "write_bench_kgroup");
    (void)engine->GroupCommit();
  }

  {
    const auto dir =
        (std::filesystem::temp_directory_path() / "ebtree_bench_write_ksync")
            .string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    ebtree::EngineOptions opts =
        ebtree::EngineOptions::EnterpriseDefaults(dir);
    std::unique_ptr<ebtree::Engine> engine;
    if (!ebtree::Engine::Open(opts, &engine).ok()) {
      std::fprintf(stderr, "kSync open failed\n");
      return 1;
    }
    (void)RunWriteBench(engine.get(), kOps, "write_bench_ksync");
    const ebtree::EngineStats stats = engine->stats();
    std::printf("write_bench_ksync fsync_merge_ratio=%llu batch=%llu waiters=%llu\n",
                static_cast<unsigned long long>(stats.fsync_merge_ratio),
                static_cast<unsigned long long>(stats.fsync_batch_total),
                static_cast<unsigned long long>(stats.fsync_waiter_total));

    constexpr int kThreadOps = 10000;
    constexpr int kThreads = 4;
    std::vector<std::thread> pool;
    pool.reserve(kThreads);
    const auto mt_start = std::chrono::steady_clock::now();
    for (int t = 0; t < kThreads; ++t) {
      pool.emplace_back([engine = engine.get(), t, kThreadOps]() {
        for (int i = 0; i < kThreadOps; ++i) {
          const std::string key =
              "t" + std::to_string(t) + "_k" + std::to_string(i);
          if (!engine->Put(key, "v").ok()) std::exit(1);
        }
      });
    }
    for (auto& th : pool) th.join();
    const auto mt_elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - mt_start);
    const double mt_sec = static_cast<double>(mt_elapsed.count()) / 1e6;
    const double mt_ops = static_cast<double>(kThreadOps * kThreads) / mt_sec;
    const ebtree::EngineStats mt_stats = engine->stats();
    std::printf(
        "write_bench_ksync_%dthread ops=%d elapsed_sec=%.3f ops_per_sec=%.0f "
        "fsync_merge_ratio=%llu\n",
        kThreads, kThreadOps * kThreads, mt_sec, mt_ops,
        static_cast<unsigned long long>(mt_stats.fsync_merge_ratio));
  }

  {
    const auto dir =
        (std::filesystem::temp_directory_path() / "ebtree_bench_write_kbalanced")
            .string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    ebtree::EngineOptions opts =
        ebtree::EngineOptions::ProductionDefaults(dir);
    std::unique_ptr<ebtree::Engine> engine;
    if (!ebtree::Engine::Open(opts, &engine).ok()) {
      std::fprintf(stderr, "kBalanced open failed\n");
      return 1;
    }
    constexpr int kBalancedOps = 10000;
    (void)RunWriteBench(engine.get(), kBalancedOps, "write_bench_kbalanced");
    const ebtree::EngineStats stats = engine->stats();
    std::printf(
        "write_bench_kbalanced fsync_merge_ratio=%llu batch=%llu waiters=%llu\n",
        static_cast<unsigned long long>(stats.fsync_merge_ratio),
        static_cast<unsigned long long>(stats.fsync_batch_total),
        static_cast<unsigned long long>(stats.fsync_waiter_total));

    const int kThreads = BalancedConcurrentThreads();
    const int kTotalOps = 100000;
    const int kThreadOps = (kTotalOps + kThreads - 1) / kThreads;
    std::vector<std::thread> pool;
    pool.reserve(kThreads);
    const auto mt_start = std::chrono::steady_clock::now();
    for (int t = 0; t < kThreads; ++t) {
      pool.emplace_back([engine = engine.get(), t, kThreadOps]() {
        for (int i = 0; i < kThreadOps; ++i) {
          const std::string key =
              "bt" + std::to_string(t) + "_k" + std::to_string(i);
          if (!engine->Put(key, "v").ok()) std::exit(1);
        }
      });
    }
    for (auto& th : pool) th.join();
    const auto mt_elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - mt_start);
    const double mt_sec = static_cast<double>(mt_elapsed.count()) / 1e6;
    const int actual_ops = kThreadOps * kThreads;
    const double mt_ops = static_cast<double>(actual_ops) / mt_sec;
    const ebtree::EngineStats mt_stats = engine->stats();
    std::printf(
        "write_bench_kbalanced_%dthread ops=%d elapsed_sec=%.3f ops_per_sec=%.0f "
        "fsync_merge_ratio=%llu target_100k=%s\n",
        kThreads, actual_ops, mt_sec, mt_ops,
        static_cast<unsigned long long>(mt_stats.fsync_merge_ratio),
        mt_ops >= 100000.0 ? "PASS" : "FAIL");
  }

  return 0;
}
