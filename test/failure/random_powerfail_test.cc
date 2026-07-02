#include <gtest/gtest.h>

#include <random>

#include "engine_test_util.h"
#include "powerfail_fuzz.h"

namespace {

int FuzzTrials() {
#if defined(EBTEST_CI)
  return 4;
#else
  return 16;
#endif
}

size_t FuzzOpCount() {
#if defined(EBTEST_CI)
  return 80;
#else
  return 200;
#endif
}

size_t EnterpriseFuzzOpCount() {
#if defined(EBTEST_CI)
  return 30;
#else
  return 80;
#endif
}

int EnterpriseFuzzTrials() {
#if defined(EBTEST_CI)
  return 4;
#else
  return 8;
#endif
}

int ConcurrentWriters() {
#if defined(EBTEST_CI)
  return 8;
#else
  return 16;
#endif
}

int ConcurrentOpsPerThread() {
#if defined(EBTEST_CI)
  return 32;
#else
  return 64;
#endif
}

int MidCheckpointTrials() {
#if defined(EBTEST_CI)
  return 4;
#else
  return 12;
#endif
}

int FourShardTrials() {
#if defined(EBTEST_CI)
  return 8;
#else
  return 16;
#endif
}

size_t FourShardOpCount() {
#if defined(EBTEST_CI)
  return 120;
#else
  return 300;
#endif
}

}  // namespace

TEST(EbFailureRandomPowerfail, ProductionRandomDestroyFuzz) {
  const std::string dir = ebtree::test::TempDir("rand_pf_prod");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  ebtree::test::RunRandomPowerfailFuzz(opts, 1001, FuzzTrials(), FuzzOpCount());
}

TEST(EbFailureRandomPowerfail, EnterpriseRandomDestroyFuzz) {
  const std::string dir = ebtree::test::TempDir("rand_pf_ent");
  ebtree::EngineOptions opts = ebtree::EngineOptions::EnterpriseDefaults(dir);
  ebtree::test::RunRandomPowerfailFuzz(opts, 2002, EnterpriseFuzzTrials(),
                                         EnterpriseFuzzOpCount());
}

TEST(EbFailureRandomPowerfail, KBalancedConcurrentRandomDestroy) {
  const std::string dir = ebtree::test::TempDir("rand_pf_conc");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  ebtree::test::RunConcurrentRandomPowerfail(opts, 3003, ConcurrentWriters(),
                                             ConcurrentOpsPerThread(), 250);
}

TEST(EbFailureRandomPowerfail, KGroupRandomDestroyStrict) {
  const std::string dir = ebtree::test::TempDir("rand_pf_group");
  ebtree::EngineOptions opts = ebtree::EngineOptions::BenchmarkGroupDefaults(dir);
  opts.group_commit_batch_size = 16;
  ebtree::test::RunRandomPowerfailFuzz(opts, 4004, FuzzTrials(), FuzzOpCount());
}

TEST(EbFailureRandomPowerfail, RandomDestroyWithCheckpointOps) {
  const std::string dir = ebtree::test::TempDir("rand_pf_cp");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  std::mt19937 rng(5005);
  std::uniform_int_distribution<size_t> destroy_dist(1, FuzzOpCount());
  for (int t = 0; t < FuzzTrials(); ++t) {
    const auto subdir = dir + "/t" + std::to_string(t);
    ebtree::EngineOptions trial = opts;
    trial.path = subdir;
    ebtree::test::RunRandomPowerfailOnce(trial, static_cast<uint32_t>(5005 + t),
                                         FuzzOpCount(), destroy_dist(rng),
                                         true);
  }
}

TEST(EbFailureRandomPowerfail, MidCheckpointRandomDestroy) {
  const std::string dir = ebtree::test::TempDir("rand_pf_mid_cp");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  std::mt19937 rng(6006);
  std::uniform_int_distribution<int> phase_dist(0, 5);
  std::uniform_int_distribution<int> key_dist(0, 99);

  for (int t = 0; t < MidCheckpointTrials(); ++t) {
    const auto subdir = dir + "/t" + std::to_string(t);
    ebtree::EngineOptions trial = opts;
    trial.path = subdir;

    ebtree::test::CommittedOracle oracle(trial.durability);
    const std::string key = "mc" + std::to_string(key_dist(rng));
    const std::string value = "mv" + std::to_string(t);

    {
      std::unique_ptr<ebtree::Engine> engine;
      ASSERT_TRUE(ebtree::Engine::Open(trial, &engine).ok());
      ASSERT_TRUE(engine->Put(key, value).ok());
      oracle.OnPutOk(engine.get(), key, value);

      const int target = phase_dist(rng);
      const ebtree::CheckpointPhase phases[] = {
          ebtree::CheckpointPhase::AfterFlush,
          ebtree::CheckpointPhase::AfterVcsFold,
          ebtree::CheckpointPhase::AfterTLog,
          ebtree::CheckpointPhase::BeforeSuperBlock,
          ebtree::CheckpointPhase::AfterSuperBlock,
          ebtree::CheckpointPhase::BeforeWalTruncate,
      };
      const ebtree::CheckpointPhase phase = phases[target];
      engine->SetCheckpointHookForTest([phase](ebtree::CheckpointPhase p) {
        return p == phase;
      });
      const ebtree::Status cp = engine->Checkpoint();
      EXPECT_FALSE(cp.ok());
    }

    std::unique_ptr<ebtree::Engine> reopened;
    ASSERT_TRUE(ebtree::Engine::Open(trial, &reopened).ok());
    const auto verify = oracle.VerifyEngine(reopened.get());
    ASSERT_TRUE(verify) << verify.message();
  }
}

TEST(EbFailureRandomPowerfail, FourShardProductionRandomDestroy) {
  const std::string dir = ebtree::test::TempDir("rand_pf_4shard");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  opts.shard_count = 4;
  ebtree::test::RunRandomPowerfailFuzz(opts, 7007, FourShardTrials(),
                                         FourShardOpCount());
}

TEST(EbFailureRandomPowerfail, StandardRandomDestroyFuzz) {
  const std::string dir = ebtree::test::TempDir("rand_pf_standard");
  ebtree::EngineOptions opts = ebtree::EngineOptions::StandardDefaults(dir);
  ebtree::test::RunRandomPowerfailFuzz(opts, 8001, FuzzTrials(), FuzzOpCount(),
                                         true, true);
}

TEST(EbFailureRandomPowerfail, StandardMidCheckpointRandomDestroy) {
  const std::string dir = ebtree::test::TempDir("rand_pf_standard_mid_cp");
  ebtree::EngineOptions opts = ebtree::EngineOptions::StandardDefaults(dir);
  std::mt19937 rng(8002);
  std::uniform_int_distribution<int> phase_dist(0, 5);
  std::uniform_int_distribution<int> key_dist(0, 99);

  for (int t = 0; t < MidCheckpointTrials(); ++t) {
    const auto subdir = dir + "/t" + std::to_string(t);
    ebtree::EngineOptions trial = opts;
    trial.path = subdir;

    ebtree::test::CommittedOracle oracle(trial.durability);
    const std::string key = "smc" + std::to_string(key_dist(rng));
    std::string value(320, 'z');

    {
      std::unique_ptr<ebtree::Engine> engine;
      ASSERT_TRUE(ebtree::Engine::Open(trial, &engine).ok());
      ASSERT_TRUE(engine->Put(key, value).ok());
      oracle.OnPutOk(engine.get(), key, value);

      const int target = phase_dist(rng);
      const ebtree::CheckpointPhase phases[] = {
          ebtree::CheckpointPhase::AfterFlush,
          ebtree::CheckpointPhase::AfterVcsFold,
          ebtree::CheckpointPhase::AfterTLog,
          ebtree::CheckpointPhase::BeforeSuperBlock,
          ebtree::CheckpointPhase::AfterSuperBlock,
          ebtree::CheckpointPhase::BeforeWalTruncate,
      };
      const ebtree::CheckpointPhase phase = phases[target];
      engine->SetCheckpointHookForTest([phase](ebtree::CheckpointPhase p) {
        return p == phase;
      });
      const ebtree::Status cp = engine->Checkpoint();
      EXPECT_FALSE(cp.ok());
    }

    std::unique_ptr<ebtree::Engine> reopened;
    ASSERT_TRUE(ebtree::Engine::Open(trial, &reopened).ok());
    const auto verify = oracle.VerifyEngine(reopened.get());
    ASSERT_TRUE(verify) << verify.message();
    EXPECT_EQ(reopened->stats().unexpected_path_total, 0u);
  }
}

TEST(EbFailureRandomPowerfail, StandardFourShardRandomDestroy) {
  const std::string dir = ebtree::test::TempDir("rand_pf_standard_4shard");
  ebtree::EngineOptions opts = ebtree::EngineOptions::StandardDefaults(dir);
  opts.shard_count = 4;
  ebtree::test::RunRandomPowerfailFuzz(opts, 8003, FourShardTrials(),
                                         FourShardOpCount(), true, true);
}
