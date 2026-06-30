#include <gtest/gtest.h>

#include "engine_test_util.h"

TEST(EbPipelineGc, ActiveReclaimSwap) {
  const std::string dir = ebtree::test::TempDir("gc_swap");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.gc_reclaim_threshold_bytes = 64;
  auto engine = ebtree::test::OpenEngineWithOptions(dir, opts);
  ASSERT_NE(engine, nullptr);
  for (int i = 0; i < 20; ++i) {
    const std::string key = "g" + std::to_string(i);
    ASSERT_TRUE(engine->Put(key, "payload_data_here").ok());
    ASSERT_TRUE(engine->Flush().ok());
  }
  EXPECT_GE(engine->stats().gc_region_swap_total, 1u);
  EXPECT_TRUE(engine->gc()->active_generation() == 0 ||
              engine->gc()->active_generation() == 1);
}

TEST(EbPipelineGc, ReclaimPreservesVisibleData) {
  const std::string dir = ebtree::test::TempDir("gc_visible");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.gc_reclaim_threshold_bytes = 64;
  auto engine = ebtree::test::OpenEngineWithOptions(dir, opts);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("keep", "value").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  for (int i = 0; i < 30; ++i) {
    ASSERT_TRUE(engine->Put("x" + std::to_string(i), "yyyyyyyy").ok());
    ASSERT_TRUE(engine->Flush().ok());
  }
  std::string value;
  ASSERT_TRUE(engine->Get("keep", &value).ok());
  EXPECT_EQ(value, "value");
}

TEST(EbPipelineGc, SwapNoPause) {
  const std::string dir = ebtree::test::TempDir("gc_nopause");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.gc_reclaim_threshold_bytes = 48;
  auto engine = ebtree::test::OpenEngineWithOptions(dir, opts);
  ASSERT_NE(engine, nullptr);
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(engine->Put("p" + std::to_string(i), "zzzzzzzzzz").ok());
    ASSERT_TRUE(engine->Flush().ok());
    std::string value;
    ASSERT_TRUE(engine->Get("p" + std::to_string(i), &value).ok());
  }
}

TEST(EbPipelineGc, ReclaimGenerationInvisibleAfterReopen) {
  const std::string dir = ebtree::test::TempDir("gc_reclaim_invisible");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.gc_reclaim_threshold_bytes = 64;
  {
    auto engine = ebtree::test::OpenEngineWithOptions(dir, opts);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("ghost", "old").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    for (int i = 0; i < 25; ++i) {
      ASSERT_TRUE(engine->Put("z" + std::to_string(i), "xxxxxxxx").ok());
      ASSERT_TRUE(engine->Flush().ok());
    }
    ASSERT_TRUE(engine->Put("ghost", "new").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngineWithOptions(dir, opts);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("ghost", &value).ok());
  EXPECT_EQ(value, "new");
}
