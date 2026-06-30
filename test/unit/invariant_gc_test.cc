#include <gtest/gtest.h>

#include "engine_test_util.h"

TEST(EbInvariantGc, LoadAllIgnoresReclaimGeneration) {
  const std::string dir = ebtree::test::TempDir("inv_gc_gen");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.gc_reclaim_threshold_bytes = 64;
  auto engine = ebtree::test::OpenEngineWithOptions(dir, opts);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("k", "v0").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  for (int i = 0; i < 30; ++i) {
    ASSERT_TRUE(engine->Put("fill" + std::to_string(i), "yyyyyyyy").ok());
    ASSERT_TRUE(engine->Flush().ok());
  }
  ASSERT_TRUE(engine->Put("k", "v1").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  engine.reset();

  auto reopened = ebtree::test::OpenEngineWithOptions(dir, opts);
  ASSERT_NE(reopened, nullptr);
  std::string value;
  ASSERT_TRUE(reopened->Get("k", &value).ok());
  EXPECT_EQ(value, "v1");
}
