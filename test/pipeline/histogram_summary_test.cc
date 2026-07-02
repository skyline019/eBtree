#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "ebtree/common/config.h"
#include "ebtree/engine/engine.h"

TEST(EbHistogramSummary, ProductionDefaultsPreferHistogram) {
  const std::string dir = ebtree::test::TempDir("histogram_summary");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  EXPECT_TRUE(opts.prefer_histogram_summary);
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  for (int i = 0; i < 200; ++i) {
    ASSERT_TRUE(engine->Put("hk" + std::to_string(i), "hv").ok());
  }
  ASSERT_TRUE(engine->Checkpoint().ok());
  EXPECT_GT(engine->btree()->pages_touched(), 0u);
}
