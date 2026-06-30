#include <gtest/gtest.h>

#include <chrono>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"

TEST(EbPipelineLazyGet, IndexedRestoreBudget) {
  const std::string dir = ebtree::test::TempDir("lazy_get_idx");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 5000; ++i) {
      ASSERT_TRUE(engine->Put("lg" + std::to_string(i), "lv").ok());
    }
  }
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.recovery_strategy = ebtree::RecoveryStrategy::kFastOpen;
  const auto start = std::chrono::steady_clock::now();
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  std::string value;
  ASSERT_TRUE(engine->Get("lg2500", &value).ok());
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  EXPECT_EQ(value, "lv");
  EXPECT_EQ(engine->stats().wal_full_scan_total, 0u);
  EXPECT_LT(elapsed.count(), 5000);
}
