#include <gtest/gtest.h>

#include <string>

#include "engine_test_util.h"

TEST(EbPipelineMmap, Reopen10kViaMmap) {
  const std::string dir = ebtree::test::TempDir("mmap_recovery_10k");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 10000; ++i) {
      const std::string key = "mk" + std::to_string(i);
      const std::string value = "mv" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, value).ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  EXPECT_NE(engine->shard(0)->mmap_manager(), nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("mk9999", &value).ok());
  EXPECT_EQ(value, "mv9999");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}
