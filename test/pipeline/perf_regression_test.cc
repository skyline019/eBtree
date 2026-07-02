#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>

#include <thread>
#include <vector>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"
#include "rar_monitor.h"

namespace {

double RunConcurrentBalancedWrite100k(const ebtree::EngineOptions& base_opts) {
  const std::string dir = ebtree::test::TempDir("perf_write_100k");
  ebtree::EngineOptions opts = base_opts;
  opts.path = dir;
  std::unique_ptr<ebtree::Engine> engine;
  if (!ebtree::Engine::Open(opts, &engine).ok()) return 0.0;
  const unsigned hw = std::thread::hardware_concurrency();
  int kThreads = hw == 0 ? 128 : static_cast<int>(hw) * 16;
  if (kThreads < 64) kThreads = 64;
  if (kThreads > 128) kThreads = 128;
  const int kTotalOps = 100000;
  const int kPerThread = (kTotalOps + kThreads - 1) / kThreads;
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
  if (put_failures.load() != 0) return 0.0;
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);
  if (elapsed.count() <= 0) return 0.0;
  return static_cast<double>(kPerThread * kThreads) /
         (static_cast<double>(elapsed.count()) / 1e6);
}

int64_t RunLazyScan10kMs(const ebtree::EngineOptions& base_opts,
                         const std::string& value) {
  const std::string dir = ebtree::test::TempDir("perf_lazy_scan_10k");
  ebtree::EngineOptions opts = base_opts;
  opts.path = dir;
  opts.background_summary_validate = false;
  opts.background_flush = false;
  {
    std::unique_ptr<ebtree::Engine> engine;
    if (!ebtree::Engine::Open(opts, &engine).ok()) return -1;
    for (int i = 0; i < 10000; ++i) {
      if (!engine->Put("lsk" + std::to_string(i), value).ok()) return -1;
    }
    if (!engine->Checkpoint().ok()) return -1;
  }
  std::unique_ptr<ebtree::Engine> engine;
  if (!ebtree::Engine::Open(opts, &engine).ok()) return -1;
  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "lsk0";
  plan.range_end = "lsk9999";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  const auto start = std::chrono::steady_clock::now();
  if (!engine->Scan(plan, &rows).ok() || rows.size() != 10000u) return -1;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - start)
      .count();
}

}  // namespace

TEST(EbPipelinePerf, FastOpen10kReleaseBudget) {
  const std::string dir = ebtree::test::TempDir("perf_rto_10k");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 10000; ++i) {
      const std::string key = "rk" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, "rv").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  const auto start = std::chrono::steady_clock::now();
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  ASSERT_NE(engine, nullptr);
#if defined(EBTEST_CI)
  EXPECT_LT(elapsed.count(), 150);
#else
  EXPECT_LT(elapsed.count(), 200);
#if defined(NDEBUG)
  EXPECT_LT(elapsed.count(), 80);
#endif
#endif
}

TEST(EbPipelinePerf, IncrementalSummaryPutBudget) {
  const std::string dir = ebtree::test::TempDir("perf_summary_puts");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 5000; ++i) {
    ASSERT_TRUE(engine->Put("sp" + std::to_string(i), "sv").ok());
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
#if defined(NDEBUG)
  EXPECT_LT(elapsed.count(), 6500);
#else
  EXPECT_LT(elapsed.count(), 5000);
#endif
}

TEST(EbPipelinePerf, KSyncWrite10kSmokeBudget) {
  const std::string dir = ebtree::test::TempDir("perf_ksync_write_10k");
  ebtree::EngineOptions opts = ebtree::EngineOptions::EnterpriseDefaults(dir);
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 10000; ++i) {
    ASSERT_TRUE(engine->Put("wk" + std::to_string(i), "wv").ok());
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
#if defined(NDEBUG)
  EXPECT_LT(elapsed.count(), 8000);
#else
  EXPECT_LT(elapsed.count(), 20000);
#endif
}

TEST(EbPipelinePerf, KBalancedWrite100kConcurrentBudget) {
  const std::string dir = ebtree::test::TempDir("perf_kbalanced_write_100k");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  const unsigned hw = std::thread::hardware_concurrency();
  int kThreads = hw == 0 ? 128 : static_cast<int>(hw) * 16;
  if (kThreads < 64) kThreads = 64;
  if (kThreads > 128) kThreads = 128;
  const int kTotalOps = 100000;
  const int kPerThread = (kTotalOps + kThreads - 1) / kThreads;
  std::vector<std::thread> pool;
  pool.reserve(kThreads);
  const auto start = std::chrono::steady_clock::now();
  for (int t = 0; t < kThreads; ++t) {
    pool.emplace_back([engine = engine.get(), t, kPerThread]() {
      for (int i = 0; i < kPerThread; ++i) {
        ASSERT_TRUE(
            engine->Put("bk" + std::to_string(t) + "_" + std::to_string(i), "v")
                .ok());
      }
    });
  }
  for (auto& th : pool) th.join();
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);
  const double ops_per_sec =
      static_cast<double>(kPerThread * kThreads) /
      (static_cast<double>(elapsed.count()) / 1e6);
#if defined(EBTEST_CI)
  EXPECT_GE(ops_per_sec, 85000.0);
  EXPECT_GE(engine->stats().fsync_merge_ratio, 8u);
#elif defined(NDEBUG)
  EXPECT_GE(ops_per_sec, 100000.0);
  EXPECT_GE(engine->stats().fsync_merge_ratio, 8u);
#else
  EXPECT_GT(ops_per_sec, 1000.0);
#endif
}

TEST(EbPipelinePerf, LazyScan10kBudget) {
  const std::string dir = ebtree::test::TempDir("perf_lazy_scan_10k");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  opts.background_summary_validate = false;
  opts.background_flush = false;
  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    for (int i = 0; i < 10000; ++i) {
      ASSERT_TRUE(engine->Put("lsk" + std::to_string(i), "lsv").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "lsk0";
  plan.range_end = "lsk9999";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  const auto start = std::chrono::steady_clock::now();
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  EXPECT_EQ(rows.size(), 10000u);
#if defined(NDEBUG)
#if defined(EBTEST_CI)
  EXPECT_LE(elapsed.count(), 60);
#else
  EXPECT_LE(elapsed.count(), 40);
#endif
#else
  EXPECT_LT(elapsed.count(), 200);
#endif
}

TEST(EbPipelinePerf, KSyncScan10kSmokeBudget) {
  const std::string dir = ebtree::test::TempDir("perf_ksync_scan_10k");
  ebtree::EngineOptions opts = ebtree::EngineOptions::EnterpriseDefaults(dir);
  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    for (int i = 0; i < 10000; ++i) {
      ASSERT_TRUE(engine->Put("sk" + std::to_string(i), "sv").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "sk0";
  plan.range_end = "sk9999";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  const auto start = std::chrono::steady_clock::now();
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  EXPECT_EQ(rows.size(), 10000u);
#if defined(NDEBUG)
  const int64_t budget_ms =
      opts.lazy_committed_load ? 40 : 15;
  EXPECT_LE(elapsed.count(), budget_ms);
#else
  EXPECT_LT(elapsed.count(), 200);
#endif
}

TEST(EbPipelinePerf, StandardWrite100kConcurrentBudget) {
  const double raw_ops =
      RunConcurrentBalancedWrite100k(ebtree::EngineOptions::ProductionDefaults(""));
  const double standard_ops =
      RunConcurrentBalancedWrite100k(ebtree::EngineOptions::StandardDefaults(""));
  ASSERT_GT(raw_ops, 0.0);
  ASSERT_GT(standard_ops, 0.0);
#if defined(NDEBUG)
  EXPECT_GE(standard_ops, raw_ops * 0.92);
#if defined(EBTEST_CI)
  EXPECT_GE(raw_ops, 10000.0);
#else
  EXPECT_GE(raw_ops, 50000.0);
#endif
#endif
}

TEST(EbPipelinePerf, StandardLazyScan10kBudget) {
  const std::string payload(256, 'q');
  const int64_t raw_ms = RunLazyScan10kMs(
      ebtree::EngineOptions::ProductionDefaults(""), "lsv");
  const int64_t standard_ms = RunLazyScan10kMs(
      ebtree::EngineOptions::StandardDefaults(""), payload);
  ASSERT_GE(raw_ms, 0);
  ASSERT_GE(standard_ms, 0);
#if defined(NDEBUG)
#if defined(EBTEST_CI)
  EXPECT_LE(raw_ms, 60);
  EXPECT_LE(standard_ms, static_cast<int64_t>(raw_ms * 1.25) + 5);
#else
  EXPECT_LE(raw_ms, 40);
  EXPECT_LE(standard_ms, static_cast<int64_t>(raw_ms * 1.10) + 2);
#endif
#endif
}

#ifndef NDEBUG
TEST(EbPipelinePerf, KBalancedWrite100kWithRarMonitor) {
  GTEST_SKIP() << "perf gate runs in Release only";
}
#else
double RunConcurrentBalancedWrite100kWithRarMonitor(
    const ebtree::EngineOptions& base_opts) {
  const std::string dir = ebtree::test::TempDir("perf_write_100k_rar");
  ebtree::EngineOptions opts = base_opts;
  opts.path = dir;
  ebtree::audit::RarMonitorOptions rar_opts{};
  rar_opts.enabled = true;
  rar_opts.chain_path = dir + "/ebtree.rar.chain.jsonl";
  rar_opts.write_circuit = true;
  rar_opts.runtime_policy.require_unexpected_path_zero = true;
  std::unique_ptr<ebtree::Engine> engine;
  ebtree::audit::RarMonitor monitor;
  if (!ebtree::audit::OpenWithRarMonitor(opts, rar_opts, &engine, &monitor)
           .ok()) {
    return 0.0;
  }
  const unsigned hw = std::thread::hardware_concurrency();
  int kThreads = hw == 0 ? 128 : static_cast<int>(hw) * 16;
  if (kThreads < 64) kThreads = 64;
  if (kThreads > 128) kThreads = 128;
  const int kTotalOps = 100000;
  const int kPerThread = (kTotalOps + kThreads - 1) / kThreads;
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
  if (put_failures.load() != 0) return 0.0;
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);
  if (elapsed.count() <= 0) return 0.0;
  return static_cast<double>(kPerThread * kThreads) /
         (static_cast<double>(elapsed.count()) / 1e6);
}

TEST(EbPipelinePerf, KBalancedWrite100kWithRarMonitor) {
  const double raw_ops = RunConcurrentBalancedWrite100k(
      ebtree::EngineOptions::ProductionDefaults(""));
  const double monitored_ops = RunConcurrentBalancedWrite100kWithRarMonitor(
      ebtree::EngineOptions::ProductionDefaults(""));
  ASSERT_GT(raw_ops, 0.0);
  ASSERT_GT(monitored_ops, 0.0);
  EXPECT_GE(monitored_ops, raw_ops * 0.99);
}
#endif
