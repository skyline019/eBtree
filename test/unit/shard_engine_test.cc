#include <gtest/gtest.h>

#include "engine_test_util.h"

TEST(EbUnitShardEngine, SingleShardEquivalentToP5) {
  const std::string dir = ebtree::test::TempDir("shard_smoke");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    EXPECT_EQ(engine->shard_count(), 1u);
    ASSERT_TRUE(engine->Put("p5k", "p5v").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("p5k", &value).ok());
  EXPECT_EQ(value, "p5v");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}
