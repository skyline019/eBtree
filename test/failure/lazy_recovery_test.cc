#include <gtest/gtest.h>

#include "engine_test_util.h"

TEST(EbFailureLazyRecovery, FastOpenSkipsWalReplay) {
  const std::string dir = ebtree::test::TempDir("lazy_fast_open");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("deferred", "v").ok());
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  EXPECT_TRUE(engine->wal_replay_pending());
  std::string value;
  ASSERT_TRUE(engine->Get("deferred", &value).ok());
  EXPECT_EQ(value, "v");
  EXPECT_FALSE(engine->wal_replay_pending());
}

TEST(EbFailureLazyRecovery, DeferredWalReplayOnFirstWrite) {
  const std::string dir = ebtree::test::TempDir("lazy_deferred_put");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("a", "1").ok());
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  EXPECT_TRUE(engine->wal_replay_pending());
  ASSERT_TRUE(engine->Put("b", "2").ok());
  EXPECT_FALSE(engine->wal_replay_pending());
  std::string value;
  ASSERT_TRUE(engine->Get("a", &value).ok());
  EXPECT_EQ(value, "1");
}

TEST(EbFailureLazyRecovery, LazyModeSingleKeyRestore) {
  const std::string dir = ebtree::test::TempDir("lazy_single_key");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("k1", "v1").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->Put("k2", "v2").ok());
    ASSERT_TRUE(engine->CorruptRootForTest().ok());
    ASSERT_TRUE(engine->CommitSuperBlockInternal().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  EXPECT_EQ(engine->recovery_mode(), ebtree::RecoveryMode::kLazy);
  std::string value;
  ASSERT_TRUE(engine->Get("k1", &value).ok());
  EXPECT_EQ(value, "v1");
  ASSERT_TRUE(engine->Get("k2", &value).ok());
  EXPECT_EQ(value, "v2");
  EXPECT_GE(engine->stats().lazy_page_faults, 1u);
}

TEST(EbFailureLazyRecovery, CorruptWalUsesTLogFallback) {
  const std::string dir = ebtree::test::TempDir("lazy_tlog_fb");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("saved", "ok").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->Put("lost", "x").ok());
    ASSERT_TRUE(engine->CorruptWalForTest().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("saved", &value).ok());
  EXPECT_EQ(value, "ok");
  EXPECT_TRUE(ebtree::test::IsNotFound(engine->Get("lost", &value)));
}
