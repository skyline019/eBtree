#include <gtest/gtest.h>

#include <set>
#include <string>

#include "ebtree/engine/engine.h"
#include "ebtree/engine/shard_router.h"
#include "engine_test_util.h"

TEST(EbInvariantForbidden, NoCrossShardDuplicateKey) {
  ebtree::EngineOptions opts;
  opts.path = ebtree::test::TempDir("forbidden_dup");
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.sync_on_commit = true;
  opts.shard_count = 4;

  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  for (int i = 0; i < 50; ++i) {
    const std::string key = "fk" + std::to_string(i);
    const uint32_t shard = ebtree::RouteShard(key, 4);
    ASSERT_TRUE(engine->Put(key, "v").ok());
    std::string value;
    ASSERT_TRUE(engine->Get(key, &value).ok());
    EXPECT_EQ(ebtree::RouteShard(key, 4), shard);
    for (uint32_t s = 0; s < 4; ++s) {
      if (s == shard) continue;
      const ebtree::ShardEngine* se = engine->shard(s);
      if (!se) continue;
      const auto* committed = &se->committed();
      EXPECT_EQ(committed->count(key), 0u) << key << " on shard " << s;
    }
  }
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}

TEST(EbInvariantForbidden, MmapLoadMatchesStreamNoFallback) {
  const std::string dir = ebtree::test::TempDir("forbidden_mmap");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("mm1", "mv1").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("mm1", &value).ok());
  EXPECT_EQ(value, "mv1");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}
