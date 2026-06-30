#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "ebtree/engine/engine.h"
#include "engine_test_util.h"

TEST(EbPipelineBackgroundFlush, AutoFlushWithoutManualCall) {
  const std::string dir = ebtree::test::TempDir("bg_flush");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.sync_on_commit = true;
  opts.background_flush = true;
  opts.memtable_flush_threshold_keys = 8;
  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    for (int i = 0; i < 20; ++i) {
      const std::string key = "bf" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, "bv").ok());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  std::string value;
  ASSERT_TRUE(engine->Get("bf19", &value).ok());
  EXPECT_EQ(value, "bv");
}

TEST(EbPipelineBackgroundFlush, SyncPutSurvivesDestroy) {
  const std::string dir = ebtree::test::TempDir("bg_flush_pf");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.sync_on_commit = true;
  opts.background_flush = true;
  opts.memtable_flush_threshold_keys = 4;
  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    ASSERT_TRUE(engine->Put("bg_survive", "yes").ok());
  }
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  std::string value;
  ASSERT_TRUE(engine->Get("bg_survive", &value).ok());
  EXPECT_EQ(value, "yes");
}
