#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "recovery_attestor.h"
#include "rar_builder.h"
#include "rar_types.h"

namespace ebtree {
namespace audit {
namespace {

TEST(RarRecovery, FastOpenReportsWalReplayPending) {
  const std::string dir = test::TempDir("rar_rec_fastopen");
  {
    auto engine = test::OpenEngine(dir, DurabilityClass::kBalanced);
    ASSERT_TRUE(engine);
    ASSERT_TRUE(engine->Put("fo_k", "fo_v").ok());
  }

  BuildRarOptions opts{};
  opts.engine_path = dir;
  opts.durability_tier = DurabilityClass::kBalanced;
  opts.probe_keys = {"fo_k"};

  RarReport report{};
  ASSERT_TRUE(BuildRar(opts, &report).ok());
  EXPECT_TRUE(report.recovery.wal_replay_pending);
  EXPECT_EQ(report.recovery.inferred_path, InferredRecoveryPath::kFastOpenDeferred);
  EXPECT_EQ(report.recovery.shard_state.size(), 1u);
}

TEST(RarRecovery, ProbeFindsKeyAfterOpen) {
  const std::string dir = test::TempDir("rar_rec_probe");
  {
    auto engine = test::OpenEngine(dir, DurabilityClass::kBalanced);
    ASSERT_TRUE(engine);
    ASSERT_TRUE(engine->Put("p1", "v1").ok());
  }

  BuildRarOptions opts{};
  opts.engine_path = dir;
  opts.durability_tier = DurabilityClass::kBalanced;
  opts.probe_keys = {"p1"};

  RarReport report{};
  ASSERT_TRUE(BuildRar(opts, &report).ok());
  ASSERT_EQ(report.recovery.probes.size(), 1u);
  EXPECT_TRUE(report.recovery.probes[0].found);
  EXPECT_EQ(report.recovery.probes[0].key, "p1");
  EXPECT_EQ(report.recovery.unexpected_path_total, 0u);
}

TEST(RarRecovery, LazyRootInfersLazyKey) {
  const std::string dir = test::TempDir("rar_rec_lazy");
  {
    auto engine = test::OpenEngine(dir, DurabilityClass::kSync);
    ASSERT_TRUE(engine);
    ASSERT_TRUE(engine->Put("lr1", "lv1").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->Put("lr2", "lv2").ok());
    ASSERT_TRUE(engine->CorruptRootForTest().ok());
    ASSERT_TRUE(engine->CommitSuperBlockInternal().ok());
  }

  BuildRarOptions opts{};
  opts.engine_path = dir;
  opts.durability_tier = DurabilityClass::kSync;
  opts.probe_keys = {"lr1", "lr2"};

  RarReport report{};
  ASSERT_TRUE(BuildRar(opts, &report).ok());
  ASSERT_EQ(report.recovery.probes.size(), 2u);
  EXPECT_TRUE(report.recovery.probes[0].found);
  EXPECT_TRUE(report.recovery.probes[1].found);
  EXPECT_EQ(report.recovery.unexpected_path_total, 0u);
}

}  // namespace
}  // namespace audit
}  // namespace ebtree
