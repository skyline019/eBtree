#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace test {
namespace {

TEST(BalancedDurabilityTest, PutReturnsWithStableLsn) {
  const std::string dir = TempDir("balanced_stable");
  ebtree::EngineOptions opts = ebtree::EngineOptions::StandardDefaults(dir);
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("bk", "bv").ok());
  EXPECT_GE(engine->stable_lsn(), 1u);
  EXPECT_TRUE(opts.compress_values);
  EXPECT_EQ(engine->stats().unexpected_path_total, 0u);
}

TEST(BalancedDurabilityTest, ConcurrentBatchAmortizesFsync) {
  const std::string dir = TempDir("balanced_batch");
  ebtree::EngineOptions opts = ebtree::EngineOptions::StandardDefaults(dir);
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_NE(engine, nullptr);

  constexpr int kThreads = 4;
  constexpr int kPerThread = 8;
  std::vector<std::thread> pool;
  pool.reserve(kThreads);
  std::atomic<int> failures{0};
  for (int t = 0; t < kThreads; ++t) {
    pool.emplace_back([engine = engine.get(), t, kPerThread, &failures]() {
      for (int i = 0; i < kPerThread; ++i) {
        const std::string key = "bb" + std::to_string(t) + "_" + std::to_string(i);
        if (!engine->Put(key, "v").ok()) failures.fetch_add(1);
      }
    });
  }
  for (auto& th : pool) th.join();

  EXPECT_EQ(failures.load(), 0);
  EXPECT_GE(engine->stable_lsn(), static_cast<uint64_t>(kThreads * kPerThread - 4));
  EXPECT_LE(engine->stats().fsync_batch_total, 12u);
  EXPECT_GE(engine->stats().fsync_merge_ratio, 2u);
}

TEST(BalancedDurabilityTest, SparsePutAdvancesStableAfterTimeout) {
  const std::string dir = TempDir("balanced_sparse");
  ebtree::EngineOptions opts = ebtree::EngineOptions::StandardDefaults(dir);
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("sp", "sv").ok());
  EXPECT_GE(engine->stable_lsn(), 1u);
}

}  // namespace
}  // namespace test
}  // namespace ebtree
