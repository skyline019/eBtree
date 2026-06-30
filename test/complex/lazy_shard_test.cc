#include <gtest/gtest.h>

#include <chrono>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"

TEST(EbComplexLazyShard, TwoFiftySixOpenIsLazy) {
  const std::string dir = ebtree::test::TempDir("lazy_shard_open");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.shard_count = 256;
  opts.durability = ebtree::DurabilityClass::kSync;
  const auto start = std::chrono::steady_clock::now();
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  ASSERT_NE(engine, nullptr);
  EXPECT_EQ(256u, engine->shard_count());
  EXPECT_EQ(nullptr, engine->shard(0));
#if defined(EBTEST_CI)
  EXPECT_LT(elapsed.count(), 15000);
#else
  EXPECT_LT(elapsed.count(), 2000);
#endif
}

TEST(EbComplexLazyShard, PutCreatesTargetShard) {
  const std::string dir = ebtree::test::TempDir("lazy_shard_put");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.shard_count = 4;
  opts.durability = ebtree::DurabilityClass::kSync;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_TRUE(engine->Put("lazy_key", "lazy_val").ok());
  EXPECT_NE(nullptr, engine->ShardForKey("lazy_key"));
  std::string value;
  ASSERT_TRUE(engine->Get("lazy_key", &value).ok());
  EXPECT_EQ(value, "lazy_val");
}
