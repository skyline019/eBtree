#include <gtest/gtest.h>

#include "engine_test_util.h"

TEST(EbFailureFullReplay, OpenReplaysWalImmediately) {
  const std::string dir = ebtree::test::TempDir("full_replay_open");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("fr1", "fv1").ok());
  }

  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.recovery_strategy = ebtree::RecoveryStrategy::kFullReplay;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_NE(engine, nullptr);
  EXPECT_FALSE(engine->wal_replay_pending());

  std::string value;
  ASSERT_TRUE(engine->Get("fr1", &value).ok());
  EXPECT_EQ(value, "fv1");
}

TEST(EbFailureFullReplay, MultiKeyVisibleWithoutDeferredReplay) {
  const std::string dir = ebtree::test::TempDir("full_replay_multi");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("a", "1").ok());
    ASSERT_TRUE(engine->Put("b", "2").ok());
  }

  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.recovery_strategy = ebtree::RecoveryStrategy::kFullReplay;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_NE(engine, nullptr);
  EXPECT_FALSE(engine->wal_replay_pending());

  std::string value;
  ASSERT_TRUE(engine->Get("b", &value).ok());
  EXPECT_EQ(value, "2");
}
