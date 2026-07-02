#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>

#include "ebtree/concept/datafile/datafile.h"
#include "ebtree/engine/engine.h"
#include "engine_test_util.h"

TEST(EbPipelineRto, FastOpenUnderBudget) {
  const std::string dir = ebtree::test::TempDir("rto_open");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 500; ++i) {
      const std::string key = "k" + std::to_string(i);
      const std::string value = "v" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, value).ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }

  const auto start = std::chrono::steady_clock::now();
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  ASSERT_NE(engine, nullptr);
  EXPECT_LT(elapsed.count(), 150);
#if !defined(EBTEST_CI)
  EXPECT_LT(elapsed.count(), 80);
#endif
  std::string value;
  ASSERT_TRUE(engine->Get("k499", &value).ok());
  EXPECT_EQ(value, "v499");
}

TEST(EbPipelineRto, FastOpen10kUnderBudget) {
  const std::string dir = ebtree::test::TempDir("rto_open_10k");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 10000; ++i) {
      const std::string key = "k" + std::to_string(i);
      const std::string value = "v" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, value).ok());
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
  EXPECT_LT(elapsed.count(), 450);
#if defined(NDEBUG)
  EXPECT_LT(elapsed.count(), 80);
#endif
#endif
  std::string value;
  ASSERT_TRUE(engine->Get("k9999", &value).ok());
  EXPECT_EQ(value, "v9999");
}

TEST(EbPipelineRto, FastOpen10kMultiShard) {
  const std::string dir = ebtree::test::TempDir("rto_open_10k_4shard");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.sync_on_commit = true;
  opts.shard_count = 4;
  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    for (int i = 0; i < 10000; ++i) {
      const std::string key = "mk" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, "mv").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  const auto start = std::chrono::steady_clock::now();
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
#if defined(EBTEST_CI)
  EXPECT_LT(elapsed.count(), 150);
#else
  EXPECT_LT(elapsed.count(), 450);
#endif
  std::string value;
  ASSERT_TRUE(engine->Get("mk9999", &value).ok());
  EXPECT_EQ(value, "mv");
}

TEST(EbPipelineRto, FastOpenBadBlockFallback) {
  const std::string dir = ebtree::test::TempDir("rto_bad_block");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("bb1", "bv1").ok());
    ASSERT_TRUE(engine->Put("bb2", "bv2").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    const uint64_t corrupt_offset = sizeof(ebtree::DataRecordHeader) +
                                    std::string("bb1").size() +
                                    std::string("bv1").size();
    ASSERT_TRUE(engine->CorruptDataFileForTest(corrupt_offset).ok());
  }
  const auto start = std::chrono::steady_clock::now();
  auto engine = ebtree::test::OpenEngine(dir);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("bb1", &value).ok());
  EXPECT_EQ(value, "bv1");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
#if defined(EBTEST_CI)
  EXPECT_LT(elapsed.count(), 150);
#else
  EXPECT_LT(elapsed.count(), 80);
#endif
}

TEST(EbPipelineRto, MultiShardOpenBudget) {
  const std::string dir = ebtree::test::TempDir("rto_multishard_open");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  opts.shard_count = 4;
  opts.background_summary_validate = false;
  opts.background_flush = false;
  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    for (int s = 0; s < 4; ++s) {
      for (int i = 0; i < 250; ++i) {
        const std::string key = "ms" + std::to_string(s) + "_" + std::to_string(i);
        ASSERT_TRUE(engine->Put(key, "v").ok());
      }
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  const auto start = std::chrono::steady_clock::now();
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  ASSERT_NE(engine, nullptr);
#if defined(NDEBUG)
  EXPECT_LT(elapsed.count(), 200);
#endif
  std::string value;
  ASSERT_TRUE(engine->Get("ms0_0", &value).ok());
  EXPECT_EQ(value, "v");
}
