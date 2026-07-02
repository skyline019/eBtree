#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "ebtree/concept/vcs/version_chain_store.h"
#include "ebtree/engine/engine.h"

namespace {

TEST(VcsChainTest, AppendFloorAndTombstonePath) {
  ebtree::VersionChainStore vcs;
  ASSERT_TRUE(vcs.Append("k", 10, 0).ok());
  ASSERT_TRUE(vcs.Append("k", 20, 10).ok());
  ASSERT_TRUE(vcs.Append("k", 30, 20).ok());
  EXPECT_EQ(vcs.Head("k"), 30u);
  EXPECT_EQ(vcs.Floor("k", 25), 20u);
  EXPECT_EQ(vcs.Floor("k", 10), 10u);
  EXPECT_EQ(vcs.Floor("k", 5), 0u);
}

TEST(VcsChainTest, SaveLoadRoundTrip) {
  const std::string dir = ebtree::test::TempDir("vcs_save_load");
  ebtree::VersionChainStore vcs;
  ASSERT_TRUE(vcs.Append("a", 1, 0).ok());
  ASSERT_TRUE(vcs.Append("b", 2, 0).ok());
  ASSERT_TRUE(vcs.Append("b", 4, 2).ok());
  const std::string path = dir + "/shard0.vidx";
  ASSERT_TRUE(vcs.SaveToFile(path).ok());

  ebtree::VersionChainStore loaded;
  ASSERT_TRUE(loaded.LoadFromFile(path).ok());
  EXPECT_EQ(loaded.Head("a"), 1u);
  EXPECT_EQ(loaded.Floor("b", 3), 2u);
  EXPECT_EQ(loaded.Floor("b", 4), 4u);
}

TEST(VcsChainTest, LongChainOverflowToPager) {
  const std::string dir = ebtree::test::TempDir("vcs_overflow");
  ebtree::VersionChainStore vcs;
  ASSERT_TRUE(
      vcs.OpenPager(dir + "/shard0.vcs", dir + "/shard0.vcsmeta").ok());
  uint64_t prev = 0;
  for (int i = 1; i <= 120; ++i) {
    ASSERT_TRUE(vcs.Append("long_k", static_cast<uint64_t>(i), prev).ok());
    prev = static_cast<uint64_t>(i);
  }
  EXPECT_LE(vcs.InlineNodeCount("long_k"), ebtree::kVcsInlineMax);
  EXPECT_GT(vcs.OverflowNodeCount("long_k"), 0u);
  EXPECT_EQ(vcs.Head("long_k"), 120u);
  EXPECT_EQ(vcs.Floor("long_k", 100), 100u);
  EXPECT_EQ(vcs.Floor("long_k", 5), 5u);
}

TEST(VcsChainTest, SaveLoadWithPagerRoundTrip) {
  const std::string dir = ebtree::test::TempDir("vcs_pager_rt");
  ebtree::VersionChainStore vcs;
  ASSERT_TRUE(
      vcs.OpenPager(dir + "/shard0.vcs", dir + "/shard0.vcsmeta").ok());
  uint64_t prev = 0;
  for (int i = 1; i <= 50; ++i) {
    ASSERT_TRUE(vcs.Append("k", static_cast<uint64_t>(i), prev).ok());
    prev = static_cast<uint64_t>(i);
  }
  const std::string vidx = dir + "/shard0.vidx";
  ASSERT_TRUE(vcs.SaveToFile(vidx).ok());

  ebtree::VersionChainStore loaded;
  ASSERT_TRUE(
      loaded.OpenPager(dir + "/shard0.vcs", dir + "/shard0.vcsmeta").ok());
  ASSERT_TRUE(loaded.LoadFromFile(vidx).ok());
  EXPECT_EQ(loaded.Head("k"), 50u);
  EXPECT_EQ(loaded.Floor("k", 25), 25u);
  EXPECT_GT(loaded.OverflowNodeCount("k"), 0u);
}

TEST(VcsChainTest, RebuildFromDataFileAfterUpdates) {
  const std::string dir = ebtree::test::TempDir("vcs_rebuild");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  opts.durability = ebtree::DurabilityClass::kSync;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());

  ASSERT_TRUE(engine->Put("rk", "v1").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  const auto snap = engine->CaptureSnapshot();
  ASSERT_TRUE(engine->Put("rk", "v2").ok());
  ASSERT_TRUE(engine->Flush().ok());

  std::string value;
  ASSERT_TRUE(engine->GetAtSnapshot("rk", snap, 0, &value).ok());
  EXPECT_EQ(value, "v1");
  ASSERT_TRUE(engine->Get("rk", &value).ok());
  EXPECT_EQ(value, "v2");
}

}  // namespace
