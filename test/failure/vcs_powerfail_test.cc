#include <gtest/gtest.h>

#include <random>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"
#include "snapshot_oracle.h"

namespace {

int LsvPowerfailTrials(ebtree::DurabilityClass durability) {
#if defined(EBTEST_CI)
  if (durability == ebtree::DurabilityClass::kBalanced) return 2;
  return 4;
#else
  if (durability == ebtree::DurabilityClass::kBalanced) return 2;
  return 8;
#endif
}

size_t LsvPowerfailOps() {
#if defined(EBTEST_CI)
  return 80;
#else
  return 80;
#endif
}

void RefreshOracleFromEngine(ebtree::Engine* engine,
                             ebtree::test::SnapshotOracle* oracle) {
  if (!engine || !oracle) return;
  const auto snap = engine->CaptureSnapshot();
  const uint64_t lsn = snap.ForShard(0);
  for (int k = 0; k < 32; ++k) {
    const std::string key = "k" + std::to_string(k);
    std::string value;
    if (engine->GetAtSnapshot(key, snap, 0, &value).ok()) {
      oracle->OnPut(lsn, key, value);
    } else {
      oracle->OnDelete(lsn, key);
    }
  }
}

void RunCheckpointPhaseDestroy(ebtree::DurabilityClass durability) {
  const ebtree::CheckpointPhase phases[] = {
      ebtree::CheckpointPhase::AfterFlush,
      ebtree::CheckpointPhase::AfterVcsFold,
      ebtree::CheckpointPhase::AfterTLog,
      ebtree::CheckpointPhase::BeforeSuperBlock,
      ebtree::CheckpointPhase::AfterSuperBlock,
      ebtree::CheckpointPhase::BeforeWalTruncate,
  };

  std::mt19937 rng(42);
  for (int trial = 0; trial < LsvPowerfailTrials(durability); ++trial) {
    const std::string dir =
        ebtree::test::TempDir("vcs_pf_" + std::to_string(trial));
    ebtree::EngineOptions opts =
        ebtree::test::LsvPowerfailOptions(dir, durability, false);
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    ebtree::test::SnapshotOracle oracle;

    const int phase_idx =
        static_cast<int>(rng() % (sizeof(phases) / sizeof(phases[0])));
    engine->SetCheckpointHookForTest([phase = phases[phase_idx]](
                                         ebtree::CheckpointPhase p) {
      return p == phase;
    });

    for (size_t i = 0; i < LsvPowerfailOps(); ++i) {
      const std::string key = "k" + std::to_string(i % 32);
      const std::string val = "v" + std::to_string(i);
      if ((i % 11) == 0) {
        (void)engine->Delete(key);
      } else {
        ASSERT_TRUE(engine->Put(key, val).ok());
      }
      if ((i % 17) == 0) {
        (void)engine->Checkpoint();
        engine.reset();
        ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
        engine->SetCheckpointHookForTest([phase = phases[phase_idx]](
                                             ebtree::CheckpointPhase p) {
          return p == phase;
        });
        RefreshOracleFromEngine(engine.get(), &oracle);
      }
    }

    engine.reset();
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    RefreshOracleFromEngine(engine.get(), &oracle);
    const auto snap = engine->CaptureSnapshot();
    EXPECT_TRUE(oracle.VerifyEngineAtSnapshot(engine.get(), snap, 0));
    EXPECT_EQ(engine->stats().unexpected_path_total, 0u);
  }
}

TEST(VcsPowerfailTest, SyncCheckpointPhaseDestroy) {
  RunCheckpointPhaseDestroy(ebtree::DurabilityClass::kSync);
}

TEST(VcsPowerfailTest, BalancedCheckpointPhaseDestroy) {
  RunCheckpointPhaseDestroy(ebtree::DurabilityClass::kBalanced);
}

TEST(VcsPowerfailTest, GroupCheckpointPhaseDestroy) {
  RunCheckpointPhaseDestroy(ebtree::DurabilityClass::kGroup);
}

}  // namespace
