#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"
#include "ebtree/engine/read_tier.h"

namespace {

TEST(VcsGcPinTest, CompactBelowPreservesFloorAtPin) {
  ebtree::VersionChainStore vcs;
  ASSERT_TRUE(vcs.Append("k", 10, 0).ok());
  ASSERT_TRUE(vcs.Append("k", 20, 10).ok());
  ASSERT_TRUE(vcs.Append("k", 30, 20).ok());
  vcs.CompactBelow(25);
  EXPECT_EQ(vcs.Floor("k", 25), 20u);
  EXPECT_EQ(vcs.Head("k"), 30u);
}

TEST(VcsGcPinTest, PinDefersGcSwapWhileReferencedInReclaimGen) {
  const std::string dir = ebtree::test::TempDir("vcs_gc_pin");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.gc_reclaim_threshold_bytes = 64;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());

  ASSERT_TRUE(engine->Put("pin_k", "v0").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  const auto snap = engine->CaptureSnapshot();
  engine->PinSnapshot(snap);

  for (int i = 0; i < 40; ++i) {
    ASSERT_TRUE(engine->Put("fill" + std::to_string(i), "yyyyyyyy").ok());
    ASSERT_TRUE(engine->Flush().ok());
  }
  ASSERT_TRUE(engine->Put("pin_k", "v1").ok());
  ASSERT_TRUE(engine->Flush().ok());

  const uint64_t before = engine->stats().gc_deferred_swap_total;
  ASSERT_TRUE(engine->Flush().ok());
  EXPECT_GE(engine->stats().gc_deferred_swap_total, before);

  std::string value;
  ASSERT_TRUE(engine->GetAtSnapshot("pin_k", snap, 0, &value).ok());
  EXPECT_EQ(value, "v0");

  engine->ReleaseSnapshot(snap);
}

}  // namespace
