#include <gtest/gtest.h>

#include <set>
#include <string>

#include "ebtree/engine/shard_router.h"
#include "engine_test_util.h"

namespace {

ebtree::EngineOptions MultiShardOpts(const std::string& dir, uint32_t count) {
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.sync_on_commit = true;
  opts.shard_count = count;
  return opts;
}

}  // namespace

TEST(EbComplexMultishard, FourShardPutGetAndScanMerge) {
  const std::string dir = ebtree::test::TempDir("multishard_4");
  const auto opts = MultiShardOpts(dir, 4);
  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    ASSERT_NE(engine, nullptr);
    EXPECT_EQ(engine->shard_count(), 4u);
    for (int i = 0; i < 100; ++i) {
      const std::string key = "s" + std::to_string(i);
      const std::string value = "v" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, value).ok());
      EXPECT_EQ(ebtree::RouteShard(key, 4),
                engine->shard(ebtree::RouteShard(key, 4))->shard_id());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  for (int i = 0; i < 100; ++i) {
    const std::string key = "s" + std::to_string(i);
    std::string value;
    ASSERT_TRUE(engine->Get(key, &value).ok()) << key;
    EXPECT_EQ(value, "v" + std::to_string(i));
  }
  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "s0";
  plan.range_end = "s99";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  EXPECT_EQ(rows.size(), 100u);
  for (size_t i = 1; i < rows.size(); ++i) {
    EXPECT_LT(rows[i - 1].first, rows[i].first);
  }
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}

TEST(EbComplexMultishard, RouteStableSixteenShard) {
  std::set<uint32_t> seen;
  for (int i = 0; i < 256; ++i) {
    const std::string key = "route" + std::to_string(i);
    const uint32_t a = ebtree::RouteShard(key, 16);
    const uint32_t b = ebtree::RouteShard(key, 16);
    EXPECT_EQ(a, b);
    seen.insert(a);
  }
  EXPECT_GT(seen.size(), 1u);
}

TEST(EbComplexMultishard, SameKeySingleShard) {
  const std::string key = "unique_key";
  const uint32_t shard = ebtree::RouteShard(key, 4);
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(shard, ebtree::RouteShard(key, 4));
  }
}

TEST(EbComplexMultishard, SixteenShardPutScanReopen) {
  const std::string dir = ebtree::test::TempDir("multishard_16");
  const auto opts = MultiShardOpts(dir, 16);
  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    EXPECT_EQ(engine->shard_count(), 16u);
    for (int i = 0; i < 160; ++i) {
      const std::string key = "h" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, "hv").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  for (int i = 0; i < 160; ++i) {
    const std::string key = "h" + std::to_string(i);
    std::string value;
    ASSERT_TRUE(engine->Get(key, &value).ok()) << key;
    EXPECT_EQ(value, "hv");
  }
  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "h0";
  plan.range_end = "h999";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  EXPECT_EQ(rows.size(), 160u);
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}

TEST(EbComplexMultishard, TwoFiftySixShardSmoke) {
  const std::string dir = ebtree::test::TempDir("multishard_256");
  const auto opts = MultiShardOpts(dir, 256);
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_NE(engine, nullptr);
  EXPECT_EQ(engine->shard_count(), 256u);
  for (int i = 0; i < 32; ++i) {
    const std::string key = "z256_" + std::to_string(i);
    ASSERT_TRUE(engine->Put(key, "zv").ok());
    EXPECT_EQ(ebtree::RouteShard(key, 256),
              engine->shard(ebtree::RouteShard(key, 256))->shard_id());
  }
  ASSERT_TRUE(engine->Checkpoint().ok());
  engine.reset();
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  for (int i = 0; i < 32; ++i) {
    const std::string key = "z256_" + std::to_string(i);
    std::string value;
    ASSERT_TRUE(engine->Get(key, &value).ok()) << key;
    EXPECT_EQ(value, "zv");
  }
}
