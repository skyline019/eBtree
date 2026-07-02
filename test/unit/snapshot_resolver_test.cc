#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"
#include "ebtree/engine/read_tier.h"

namespace {

TEST(SnapshotResolverTest, DeleteAfterSnapshotStillVisible) {
  const std::string dir = ebtree::test::TempDir("snap_delete");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  opts.durability = ebtree::DurabilityClass::kSync;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());

  ASSERT_TRUE(engine->Put("dk", "alive").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  const auto snap = engine->CaptureSnapshot();
  ASSERT_TRUE(engine->Delete("dk").ok());
  ASSERT_TRUE(engine->Flush().ok());

  std::string value;
  ASSERT_TRUE(engine->GetAtSnapshot("dk", snap, 0, &value).ok());
  EXPECT_EQ(value, "alive");
  EXPECT_FALSE(engine->Get("dk", &value).ok());
}

TEST(SnapshotResolverTest, VersionChainTierRecorded) {
  const std::string dir = ebtree::test::TempDir("snap_tier");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  opts.durability = ebtree::DurabilityClass::kSync;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());

  ASSERT_TRUE(engine->Put("tk", "old").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  const auto snap = engine->CaptureSnapshot();
  ASSERT_TRUE(engine->Put("tk", "new").ok());
  ASSERT_TRUE(engine->Flush().ok());

  const uint64_t before =
      engine->stats().read_tier_hits[static_cast<size_t>(
          ebtree::ReadTier::kVersionChain)];
  std::string value;
  ASSERT_TRUE(engine->GetAtSnapshot("tk", snap, 0, &value).ok());
  EXPECT_EQ(value, "old");
  const uint64_t after =
      engine->stats().read_tier_hits[static_cast<size_t>(
          ebtree::ReadTier::kVersionChain)];
  EXPECT_GT(after, before);
}

TEST(SnapshotResolverTest, DeleteTombstoneInvisibleAtSnapshot) {
  const std::string dir = ebtree::test::TempDir("snap_tomb");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  opts.durability = ebtree::DurabilityClass::kSync;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());

  ASSERT_TRUE(engine->Put("tk", "alive").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  const auto snap = engine->CaptureSnapshot();
  ASSERT_TRUE(engine->Delete("tk").ok());
  ASSERT_TRUE(engine->Flush().ok());

  std::string value;
  ASSERT_TRUE(engine->GetAtSnapshot("tk", snap, 0, &value).ok());
  EXPECT_EQ(value, "alive");
  EXPECT_FALSE(engine->Get("tk", &value).ok());
}

TEST(SnapshotResolverTest, IncompleteVcsUsesWalSnapshotTier) {
  const std::string dir = ebtree::test::TempDir("snap_wal_tier");
  ebtree::EngineOptions write_opts =
      ebtree::EngineOptions::ProductionDefaults(dir);
  write_opts.durability = ebtree::DurabilityClass::kSync;
  ebtree::SnapshotToken snap{};
  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(write_opts, &engine).ok());
    ASSERT_TRUE(engine->Put("wk", "v1").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    snap = engine->CaptureSnapshot();
    ASSERT_TRUE(engine->Put("wk", "v2").ok());
    ASSERT_TRUE(engine->Flush().ok());
  }

  ebtree::EngineOptions opts = write_opts;
  opts.lazy_committed_load = true;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_TRUE(engine->wal_replay_pending());
  ASSERT_TRUE(engine->CorruptRootForTest().ok());
  engine->ClearVcsForTest();

  std::string value;
  ASSERT_TRUE(engine->GetAtSnapshot("wk", snap, 0, &value).ok());
  EXPECT_EQ(value, "v1");
  EXPECT_TRUE(engine->wal_replay_pending());
  EXPECT_NE(engine->recovery_mode(), ebtree::RecoveryMode::kHot);
}

TEST(SnapshotResolverTest, ReadOwnTxnWriteBeforeCommit) {
  const std::string dir = ebtree::test::TempDir("snap_own_txn");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  opts.durability = ebtree::DurabilityClass::kBalanced;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());

  ASSERT_TRUE(engine->Put("rk", "base").ok());
  const auto snap = engine->CaptureSnapshot();
  const uint32_t txn_id = 42;
  ASSERT_TRUE(engine->Put("rk", "dirty", txn_id).ok());

  std::string value;
  ASSERT_TRUE(engine->GetAtSnapshot("rk", snap, txn_id, &value).ok());
  EXPECT_EQ(value, "dirty");
}

TEST(SnapshotResolverTest, ScanOwnTxnWriteBeforeCommit) {
  const std::string dir = ebtree::test::TempDir("snap_own_txn_scan");
  ebtree::EngineOptions opts = ebtree::EngineOptions::ProductionDefaults(dir);
  opts.durability = ebtree::DurabilityClass::kBalanced;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());

  ASSERT_TRUE(engine->Put("rk", "base").ok());
  const auto snap = engine->CaptureSnapshot();
  const uint32_t txn_id = 42;
  ASSERT_TRUE(engine->Put("rk", "dirty", txn_id).ok());

  ebtree::TypedPlan plan{};
  plan.op = ebtree::PredicateOp::kEq;
  plan.key = "rk";
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->ScanAtSnapshot(plan, snap, txn_id, &rows).ok());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].second, "dirty");
}

}  // namespace
