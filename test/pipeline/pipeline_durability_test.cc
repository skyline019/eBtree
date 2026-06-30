#include <gtest/gtest.h>

#include <fstream>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"

TEST(EbPipeline, WriteFlushStableLsn) {
  const std::string dir = ebtree::test::TempDir("pipeline_stable");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  const uint64_t before = engine->stable_lsn();
  ASSERT_TRUE(engine->Put("p1", "data").ok());
  ASSERT_TRUE(engine->Flush().ok());
  EXPECT_GT(engine->stable_lsn(), before);
  EXPECT_GE(engine->stats().flusher_flush_total, 1u);
}

TEST(EbPipeline, SyncDurabilitySetsStableOnPut) {
  const std::string dir = ebtree::test::TempDir("pipeline_sync");
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("sync_k", "sync_v").ok());
  EXPECT_GE(engine->stable_lsn(), 1u);
}

TEST(EbPipeline, SuperBlockAfterCheckpoint) {
  const std::string dir = ebtree::test::TempDir("pipeline_sb");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("sb", "v").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  EXPECT_GE(engine->stats().superblock_commit_total, 1u);
}

TEST(EbPipeline, PutWithoutFlushRecoveredViaWal) {
  const std::string dir = ebtree::test::TempDir("pipeline_wal_reopen");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("wal_k", "wal_v").ok());
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("wal_k", &value).ok());
  EXPECT_EQ(value, "wal_v");
}

TEST(EbPipeline, FlushWritesDataFile) {
  const std::string dir = ebtree::test::TempDir("pipeline_datafile");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("df_k", "df_v").ok());
  ASSERT_TRUE(engine->Flush().ok());
  std::ifstream in(dir + "/shard0.data", std::ios::binary);
  ASSERT_TRUE(in.good());
  char buf[4];
  in.read(buf, 4);
  EXPECT_GT(in.gcount(), 0);
}

TEST(EbPipeline, BalancedPutAdvancesStableLsn) {
  const std::string dir = ebtree::test::TempDir("pipeline_balanced");
  ebtree::EngineOptions opts = ebtree::EngineOptions::StandardDefaults(dir);
  auto engine = ebtree::test::OpenEngineWithOptions(dir, opts);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("bal_k", "bal_v").ok());
  EXPECT_GE(engine->stable_lsn(), 1u);
  EXPECT_GE(engine->stats().fsync_batch_total, 1u);
}

TEST(EbPipeline, GroupCommitAdvancesStableLsn) {
  const std::string dir = ebtree::test::TempDir("pipeline_group");
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kGroup);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("g_k", "g_v").ok());
  EXPECT_EQ(engine->stable_lsn(), 0u);
  ASSERT_TRUE(engine->GroupCommit().ok());
  EXPECT_GE(engine->stable_lsn(), 1u);
  EXPECT_GE(engine->stats().group_commit_total, 1u);
}

TEST(EbPipeline, AutoBatchGroupCommit) {
  const std::string dir = ebtree::test::TempDir("pipeline_auto_gc");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kGroup;
  opts.group_commit_batch_size = 2;
  auto engine = ebtree::test::OpenEngineWithOptions(dir, opts);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("a1", "v1").ok());
  EXPECT_EQ(engine->stats().group_commit_total, 0u);
  ASSERT_TRUE(engine->Put("a2", "v2").ok());
  EXPECT_GE(engine->stats().group_commit_total, 1u);
  EXPECT_GE(engine->stable_lsn(), 2u);
}

TEST(EbPipeline, UnflushedScanVisible) {
  const std::string dir = ebtree::test::TempDir("pipeline_scan_overlay");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("scan_k", "scan_v").ok());
  EXPECT_TRUE(engine->committed().empty());
  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kEq;
  plan.key = "scan_k";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].second, "scan_v");
}

TEST(EbPipeline, DeleteVisibleBeforeFlush) {
  const std::string dir = ebtree::test::TempDir("pipeline_del");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("del_k", "v").ok());
  ASSERT_TRUE(engine->Delete("del_k").ok());
  std::string value;
  EXPECT_TRUE(ebtree::test::IsNotFound(engine->Get("del_k", &value)));
}

TEST(EbPipeline, CommittedEmptyBeforeFlush) {
  const std::string dir = ebtree::test::TempDir("pipeline_committed");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("c_k", "c_v").ok());
  EXPECT_TRUE(engine->committed().empty());
  ASSERT_TRUE(engine->Flush().ok());
  EXPECT_FALSE(engine->committed().empty());
}
