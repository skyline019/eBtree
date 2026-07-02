#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"

TEST(VcsInvariantTest, ForwardPointerMatchesHead) {
  const std::string dir = ebtree::test::TempDir("vcs_fwd_inv");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  opts.durability = ebtree::DurabilityClass::kSync;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());

  ASSERT_TRUE(engine->Put("fwd_k", "v1").ok());
  ASSERT_TRUE(engine->Flush().ok());
  ASSERT_TRUE(engine->Put("fwd_k", "v2").ok());
  ASSERT_TRUE(engine->Flush().ok());

  ebtree::ShardEngine* shard = engine->shard(0);
  ASSERT_NE(shard, nullptr);
  uint64_t btree_lsn = 0;
  ASSERT_TRUE(shard->btree()->Get("fwd_k", &btree_lsn).ok());
  EXPECT_EQ(shard->vcs()->Head("fwd_k"), btree_lsn);
}
