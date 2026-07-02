#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "ebtree/common/config.h"
#include "ebtree/engine/engine.h"
#include "ebtree/engine/engine_attest.h"

TEST(RarAttestExportV2, ForbiddenSubsetAndCheckpointLsn) {
  const std::string dir = ebtree::test::TempDir("attest_v2");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_TRUE(engine->Put("av2k", "av2v").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());

  ebtree::AttestExportOptions aopts{};
  aopts.probe_keys = {"av2k"};
  ebtree::AttestExportReportV2 report{};
  ASSERT_TRUE(ebtree::AttestExportV2(engine.get(), aopts, &report).ok());
  EXPECT_GT(report.checkpoint_lsn, 0u);
  EXPECT_TRUE(report.forbidden_violations.empty());
  EXPECT_EQ(report.base.recovery.unexpected_path_total, 0u);
}

TEST(RarTierConsistency, ProductionOpenConsistent) {
  const std::string dir = ebtree::test::TempDir("tier_consistent");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_TRUE(engine->Put("tck", "tcv").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  ebtree::AttestExportOptions aopts{};
  aopts.probe_keys = {"tck"};
  ebtree::AttestExportReportV2 report{};
  ASSERT_TRUE(ebtree::AttestExportV2(engine.get(), aopts, &report).ok());
  ASSERT_FALSE(report.base.probes.empty());
  EXPECT_NE(report.base.probes[0].read_tier, ebtree::ReadTier::kCount);
}
