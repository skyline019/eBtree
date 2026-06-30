#include <gtest/gtest.h>

#include <filesystem>

#include "engine_test_util.h"

TEST(EbFailureDurabilityBounds, AsyncReopenWithoutFlushLosesData) {
  const std::string dir = ebtree::test::TempDir("fail_async_loss");
  {
    auto engine =
        ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kAsync);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("lost", "v").ok());
    EXPECT_EQ(engine->stable_lsn(), 0u);
    EXPECT_TRUE(engine->committed().empty());
    engine.reset();
  }
  std::filesystem::remove(dir + "/shard0.wal");
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kAsync);
  ASSERT_NE(engine, nullptr);
  std::string value;
  EXPECT_TRUE(ebtree::test::IsNotFound(engine->Get("lost", &value)));
}

TEST(EbFailureDurabilityBounds, GroupWithoutCommitReopenNeedsWal) {
  const std::string dir = ebtree::test::TempDir("fail_group_wal");
  {
    auto engine =
        ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kGroup);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("gw", "gv").ok());
    EXPECT_EQ(engine->stable_lsn(), 0u);
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kGroup);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("gw", &value).ok());
  EXPECT_EQ(value, "gv");
}
