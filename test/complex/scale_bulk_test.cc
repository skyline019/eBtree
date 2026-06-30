#include <gtest/gtest.h>

#include <cstdlib>

#include "engine_test_util.h"

namespace {

int ScaleKeyCount() {
#if defined(EBTEST_CI)
  return 10000;
#else
  return 100000;
#endif
}

}  // namespace

TEST(EbComplexScale, BulkPutCheckpointReopenSample) {
  const std::string dir = ebtree::test::TempDir("scale_bulk");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.sync_on_commit = true;
  opts.shard_count = 4;

  const int n = ScaleKeyCount();
  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    for (int i = 0; i < n; ++i) {
      const std::string key = "bulk" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, "val").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  const int samples[] = {0, n / 4, n / 2, n - 1};
  for (int idx : samples) {
    const std::string key = "bulk" + std::to_string(idx);
    std::string value;
    ASSERT_TRUE(engine->Get(key, &value).ok()) << key;
    EXPECT_EQ(value, "val");
  }
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}
