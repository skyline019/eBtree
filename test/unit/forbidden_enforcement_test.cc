#include <gtest/gtest.h>

#include <thread>

#include "ebtree/concept/tlog/tlog.h"
#include "ebtree/engine/engine.h"
#include "engine_test_util.h"

TEST(EbForbiddenEnforcement, CommittedEmptyBeforeFlush) {
  const std::string dir = ebtree::test::TempDir("forbidden_committed");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("cf1", "cv1").ok());
  EXPECT_TRUE(engine->committed().empty());
  std::string value;
  ASSERT_TRUE(engine->Get("cf1", &value).ok());
  EXPECT_EQ(value, "cv1");
}

TEST(EbForbiddenEnforcement, FastOpenDoesNotFullReplayOnGet) {
  const std::string dir = ebtree::test::TempDir("forbidden_fast_open");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("fo1", "fov").ok());
  }
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.recovery_strategy = ebtree::RecoveryStrategy::kFastOpen;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_NE(engine, nullptr);
  EXPECT_TRUE(engine->wal_replay_pending());
  std::string value;
  ASSERT_TRUE(engine->Get("fo1", &value).ok());
  EXPECT_EQ(value, "fov");
}

TEST(EbForbiddenEnforcement, TlogTailAdvancesAfterCheckpoint) {
  const std::string dir = ebtree::test::TempDir("forbidden_tlog_tail");
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("tt1", "ttv").ok());
  EXPECT_EQ(engine->stats().tlog_snapshot_total, 0u);
  ASSERT_TRUE(engine->Checkpoint().ok());
  EXPECT_GE(engine->stats().tlog_snapshot_total, 1u);
}

TEST(EbForbiddenEnforcement, GcStaleGenerationInvisible) {
  const std::string dir = ebtree::test::TempDir("forbidden_gc_gen");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.gc_reclaim_threshold_bytes = 32;
  {
    auto engine = ebtree::test::OpenEngineWithOptions(dir, opts);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("ghost", "old").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    for (int i = 0; i < 40; ++i) {
      ASSERT_TRUE(engine->Put("z" + std::to_string(i), "xxxxxxxxxxxx").ok());
      ASSERT_TRUE(engine->Flush().ok());
    }
    EXPECT_GE(engine->stats().gc_region_swap_total, 1u);
    ASSERT_TRUE(engine->Put("ghost", "new").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngineWithOptions(dir, opts);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("ghost", &value).ok());
  EXPECT_EQ(value, "new");
}

TEST(EbForbiddenEnforcement, FlashbackNoFallbackRead) {
  const std::string dir = ebtree::test::TempDir("forbidden_flashback");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    ebtree::SetTimestampSourceForTest([]() { return 8000u; });
    ASSERT_TRUE(engine->Put("fb1", "fbv").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ebtree::ResetTimestampSourceForTest();
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->GetAsOf("fb1", 8000u, &value).ok());
  EXPECT_EQ(value, "fbv");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}

TEST(EbForbiddenEnforcement, NoFallbackReadAfterCheckpoint) {
  const std::string dir = ebtree::test::TempDir("forbidden_no_fb");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("nf1", "nfv").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  engine.reset();
  engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("nf1", &value).ok());
  EXPECT_EQ(value, "nfv");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}

TEST(EbForbiddenEnforcement, PagedRootHealOnReopen) {
  const std::string dir = ebtree::test::TempDir("forbidden_paged_heal");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 20; ++i) {
      ASSERT_TRUE(
          engine->Put("ph" + std::to_string(i), "pv").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->CorruptRootForTest().ok());
    ASSERT_TRUE(engine->CommitSuperBlockInternal().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("ph10", &value).ok());
  EXPECT_EQ(value, "pv");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}

TEST(EbForbiddenEnforcement, ConcurrentReadersNoWalReplayDeferral) {
  const std::string dir = ebtree::test::TempDir("forbidden_concurrent");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("cr_anchor", "anchor").ok());
  }
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.recovery_strategy = ebtree::RecoveryStrategy::kFastOpen;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  std::vector<std::thread> readers;
  for (int t = 0; t < 8; ++t) {
    readers.emplace_back([&]() {
      for (int i = 0; i < 50; ++i) {
        std::string value;
        engine->Get("cr_anchor", &value);
        std::this_thread::yield();
      }
    });
  }
  for (auto& th : readers) th.join();
}
