#include <gtest/gtest.h>

#include "ebtree/engine/shard_router.h"
#include "engine_test_util.h"

TEST(EbPipelineMultishard, FourShardCheckpointCorruptOneWal) {
  const std::string dir = ebtree::test::TempDir("multishard_recovery");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.sync_on_commit = true;
  opts.shard_count = 4;

  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    for (int i = 0; i < 40; ++i) {
      const std::string key = "r" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, "rv").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->CorruptWalForTest(1).ok());
  }

  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  for (uint32_t sid = 0; sid < 4; ++sid) {
    if (sid == 1) continue;
    for (int i = 0; i < 40; ++i) {
      const std::string key = "r" + std::to_string(i);
      if (ebtree::RouteShard(key, 4) != sid) continue;
      std::string value;
      ASSERT_TRUE(engine->Get(key, &value).ok()) << key << " shard " << sid;
      EXPECT_EQ(value, "rv");
    }
  }
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}
