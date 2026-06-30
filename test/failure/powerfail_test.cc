#include <gtest/gtest.h>

#include "ebtree/engine/engine.h"
#include "engine_test_util.h"

TEST(EbFailurePowerfail, SyncPutSurvivesDestroyWithoutCheckpoint) {
  const std::string dir = ebtree::test::TempDir("powerfail_sync");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("survive", "yes").ok());
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("survive", &value).ok());
  EXPECT_EQ(value, "yes");
}

TEST(EbFailurePowerfail, BalancedPutSurvivesDestroy) {
  const std::string dir = ebtree::test::TempDir("powerfail_balanced");
  {
    ebtree::EngineOptions opts = ebtree::EngineOptions::StandardDefaults(dir);
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("bal_survive", "yes").ok());
    EXPECT_GE(engine->stable_lsn(), 1u);
  }
  ebtree::EngineOptions opts = ebtree::EngineOptions::StandardDefaults(dir);
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("bal_survive", &value).ok());
  EXPECT_EQ(value, "yes");
}

TEST(EbFailurePowerfail, GroupCommitSurvivesDestroy) {
  const std::string dir = ebtree::test::TempDir("powerfail_group");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kGroup);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("gc_survive", "yes").ok());
    ASSERT_TRUE(engine->GroupCommit().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kGroup);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("gc_survive", &value).ok());
  EXPECT_EQ(value, "yes");
}

TEST(EbFailurePowerfail, TruncatedWalUsesTLogFallback) {
  const std::string dir = ebtree::test::TempDir("powerfail_wal_trunc");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("tw1", "tv1").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->Put("tw2", "tv2").ok());
    ASSERT_TRUE(engine->TruncateWalForTest().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("tw1", &value).ok());
  EXPECT_EQ(value, "tv1");
}

TEST(EbFailurePowerfail, DualSlotSuperBlockElectsGoodSlot) {
  const std::string dir = ebtree::test::TempDir("powerfail_dual_slot");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("slot", "good").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->CorruptSuperBlockSlotForTest(0).ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("slot", &value).ok());
  EXPECT_EQ(value, "good");
}
