#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "powerfail_fuzz.h"
#include "rar_oracle_bridge.h"
#include "rar_builder.h"
#include "rar_types.h"

namespace ebtree {
namespace audit {
namespace {

RarReport RunRarAfterPowerfail(uint32_t seed, size_t op_count,
                               DurabilityClass tier, bool with_control_ops) {
  const std::string dir = test::TempDir("rar_fuzz_" + std::to_string(seed));
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  opts.path = dir;
  opts.durability = tier;
  if (tier == DurabilityClass::kSync) {
    opts = EngineOptions::EnterpriseDefaults(dir);
    opts.path = dir;
  } else if (tier == DurabilityClass::kGroup) {
    opts = EngineOptions::BenchmarkGroupDefaults(dir);
    opts.path = dir;
    opts.group_commit_batch_size = 16;
  }

  test::CommittedOracle oracle(tier);
  const auto ops =
      test::GeneratePowerfailOps(seed, op_count, tier, with_control_ops);
  std::unordered_map<std::string, std::string> pre_destroy;

  {
    std::unique_ptr<Engine> engine;
    EXPECT_TRUE(Engine::Open(opts, &engine).ok());
    for (const auto& op : ops) {
      if (!test::ExecuteChaosOp(engine.get(), op, &oracle).ok()) break;
    }
    pre_destroy = oracle.SnapshotVisible(engine.get());
  }

  const auto mode = with_control_ops ? ContractMode::kDurable
                                     : ContractMode::kVisibility;
  const ExpectSnapshot expect =
      test::BuildExpectFromOracle(oracle, pre_destroy, mode, opts.shard_count);

  BuildRarOptions rar_opts{};
  rar_opts.engine_path = dir;
  rar_opts.durability_tier = tier;
  rar_opts.engine_options = opts;
  rar_opts.probe_keys = test::CollectProbeKeysFromExpect(expect);
  rar_opts.expect = &expect;
  rar_opts.policy.recovery_max_missing = 0;
  rar_opts.policy.require_unexpected_path_zero = true;

  RarReport report{};
  EXPECT_TRUE(BuildRar(rar_opts, &report).ok());
  return report;
}

TEST(RarOracleEquivalence, ProductionRandomDestroy) {
  const int trials = 4;
  for (int t = 0; t < trials; ++t) {
    const RarReport report =
        RunRarAfterPowerfail(static_cast<uint32_t>(1000 + t), 80,
                             DurabilityClass::kBalanced, false);
    EXPECT_EQ(report.verdict, RarVerdict::kPass) << "trial=" << t;
    EXPECT_TRUE(report.contract.missing.empty()) << "trial=" << t;
    EXPECT_EQ(report.recovery.unexpected_path_total, 0u) << "trial=" << t;
  }
}

TEST(RarOracleEquivalence, ProductionWithControlOps) {
  const RarReport report =
      RunRarAfterPowerfail(2001, 60, DurabilityClass::kBalanced, true);
  EXPECT_EQ(report.verdict, RarVerdict::kPass);
  EXPECT_TRUE(report.contract.missing.empty());
}

TEST(RarOracleEquivalence, KGroupStrict) {
  const RarReport report =
      RunRarAfterPowerfail(3001, 80, DurabilityClass::kGroup, false);
  EXPECT_EQ(report.verdict, RarVerdict::kPass);
  EXPECT_TRUE(report.contract.missing.empty());
}

TEST(RarOracleEquivalence, MidCheckpointPattern) {
  const std::string dir = test::TempDir("rar_mid_ckpt");
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  opts.path = dir;

  test::CommittedOracle oracle(DurabilityClass::kBalanced);
  {
    std::unique_ptr<Engine> engine;
    ASSERT_TRUE(Engine::Open(opts, &engine).ok());
    ASSERT_TRUE(engine->Put("mid_k", "mid_v").ok());
    oracle.OnPutOk(engine.get(), "mid_k", "mid_v");

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> phase_dist(
        0, static_cast<int>(CheckpointPhase::AfterSuperBlock));
    engine->SetCheckpointHookForTest([](CheckpointPhase p) {
      return p == CheckpointPhase::BeforeSuperBlock;
    });
    (void)engine->Checkpoint();
  }

  const ExpectSnapshot expect = test::BuildExpectFromOracle(
      oracle, {}, ContractMode::kDurable, 1);

  BuildRarOptions rar_opts{};
  rar_opts.engine_path = dir;
  rar_opts.durability_tier = DurabilityClass::kBalanced;
  rar_opts.engine_options = opts;
  rar_opts.probe_keys = test::CollectProbeKeysFromExpect(expect);
  rar_opts.expect = &expect;

  RarReport report{};
  ASSERT_TRUE(BuildRar(rar_opts, &report).ok());
  EXPECT_EQ(report.verdict, RarVerdict::kPass);
  EXPECT_TRUE(report.contract.missing.empty());
}

TEST(RarOracleEquivalence, WalCorruptTLogFallback) {
  const std::string dir = test::TempDir("rar_tlog_fb");
  {
    auto engine = test::OpenEngine(dir, DurabilityClass::kSync);
    ASSERT_TRUE(engine);
    ASSERT_TRUE(engine->Put("tf1", "tv").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->CorruptWalForTest().ok());
  }

  BuildRarOptions opts{};
  opts.engine_path = dir;
  opts.durability_tier = DurabilityClass::kSync;
  opts.probe_keys = {"tf1"};

  RarReport report{};
  ASSERT_TRUE(BuildRar(opts, &report).ok());
  EXPECT_TRUE(report.physical.shards[0].wal.badwal_marker);
  EXPECT_EQ(report.recovery.inferred_path, InferredRecoveryPath::kTLogFallback);
  ASSERT_EQ(report.recovery.probes.size(), 1u);
  EXPECT_TRUE(report.recovery.probes[0].found);
}

TEST(RarOracleEquivalence, FourShardProductionSmoke) {
  const std::string dir = test::TempDir("rar_4shard");
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  opts.path = dir;
  opts.shard_count = 4;

  test::CommittedOracle oracle(DurabilityClass::kBalanced);
  std::unordered_map<std::string, std::string> pre_destroy;
  {
    std::unique_ptr<Engine> engine;
    ASSERT_TRUE(Engine::Open(opts, &engine).ok());
    for (int i = 0; i < 40; ++i) {
      const std::string key = "k" + std::to_string(i);
      const std::string value = "v" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, value).ok());
      oracle.OnPutOk(engine.get(), key, value);
    }
    pre_destroy = oracle.SnapshotVisible(engine.get());
  }

  const ExpectSnapshot expect = test::BuildExpectFromOracle(
      oracle, pre_destroy, ContractMode::kVisibility, 4);

  BuildRarOptions rar_opts{};
  rar_opts.engine_path = dir;
  rar_opts.durability_tier = DurabilityClass::kBalanced;
  rar_opts.engine_options = opts;
  rar_opts.shard_count = 4;
  rar_opts.probe_keys = test::CollectProbeKeysFromExpect(expect);
  rar_opts.expect = &expect;

  RarReport report{};
  ASSERT_TRUE(BuildRar(rar_opts, &report).ok());
  EXPECT_EQ(report.shard_count, 4u);
  EXPECT_EQ(report.physical.shards.size(), 4u);
  EXPECT_EQ(report.verdict, RarVerdict::kPass);
  EXPECT_TRUE(report.contract.missing.empty());
}

TEST(RarOracleEquivalence, StandardDefaultsPowerfailNoUnexpected) {
  const std::string dir = test::TempDir("rar_standard_pf");
  EngineOptions opts = EngineOptions::StandardDefaults(dir);
  test::RunRandomPowerfailOnce(opts, 9101, 120, 60, false, true, true);

  BuildRarOptions rar_opts{};
  rar_opts.engine_path = opts.path;
  rar_opts.durability_tier = DurabilityClass::kBalanced;
  rar_opts.engine_options = opts;
  rar_opts.policy.require_unexpected_path_zero = true;
  rar_opts.policy.require_tier_consistent = true;

  RarReport report{};
  ASSERT_TRUE(BuildRar(rar_opts, &report).ok());
  EXPECT_EQ(report.recovery.unexpected_path_total, 0u);
  EXPECT_EQ(report.verdict, RarVerdict::kPass);
}

}  // namespace
}  // namespace audit
}  // namespace ebtree
