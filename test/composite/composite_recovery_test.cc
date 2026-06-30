#include <gtest/gtest.h>

#include "ebtree/common/config.h"
#include "engine_test_util.h"

TEST(EbComposite, WalDataSuperBlockChain) {
  const std::string dir = ebtree::test::TempDir("composite_chain");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("chain", "link").ok());
  ASSERT_TRUE(engine->Flush().ok());
  EXPECT_GE(engine->stats().stable_lsn, 1u);
  ASSERT_TRUE(engine->CommitSuperBlockInternal().ok());
  EXPECT_GE(engine->stats().superblock_commit_total, 1u);

  std::string value;
  ASSERT_TRUE(engine->Get("chain", &value).ok());
  EXPECT_EQ(value, "link");
}

TEST(EbComposite, ReopenAfterCheckpoint) {
  const std::string dir = ebtree::test::TempDir("composite_reopen");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("persist", "yes").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("persist", &value).ok());
  EXPECT_EQ(value, "yes");
}

TEST(EbComposite, CheckpointInterruptThenReopen) {
  const std::string dir = ebtree::test::TempDir("composite_cp_interrupt");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("midcp", "value").ok());
    engine->SetCheckpointHookForTest([](ebtree::CheckpointPhase p) {
      return p == ebtree::CheckpointPhase::BeforeSuperBlock;
    });
    EXPECT_FALSE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("midcp", &value).ok());
  EXPECT_EQ(value, "value");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}

TEST(EbComposite, WalCorruptTlogFallbackScan) {
  const std::string dir = ebtree::test::TempDir("composite_wal_tlog");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("scan_k", "scan_v").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->CorruptWalForTest().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("scan_k", &value).ok());
  EXPECT_EQ(value, "scan_v");

  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kEq;
  plan.key = "scan_k";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].second, "scan_v");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}

TEST(EbComposite, FlashbackAfterCheckpointInterrupt) {
  const std::string dir = ebtree::test::TempDir("composite_fb_interrupt");
  uint32_t snap_ts = 16000;
  ebtree::SetTimestampSourceForTest([snap_ts]() { return snap_ts; });
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("fb_int", "before").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->Put("fb_int", "after").ok());
    engine->SetCheckpointHookForTest([](ebtree::CheckpointPhase p) {
      return p == ebtree::CheckpointPhase::AfterFlush;
    });
    EXPECT_FALSE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->GetAsOf("fb_int", snap_ts, &value).ok());
  EXPECT_EQ(value, "before");
  ASSERT_TRUE(engine->Get("fb_int", &value).ok());
  EXPECT_EQ(value, "after");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
  ebtree::ResetTimestampSourceForTest();
}

TEST(EbComposite, FlashbackWalCorruptScanAsOf) {
  const std::string dir = ebtree::test::TempDir("composite_fb_wal_scan");
  uint32_t snap_ts = 17000;
  ebtree::SetTimestampSourceForTest([snap_ts]() { return snap_ts; });
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("fb_s1", "one").ok());
    ASSERT_TRUE(engine->Put("fb_s2", "two").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->CorruptWalForTest().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);

  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "fb_s1";
  plan.range_end = "fb_s3";
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->ScanAsOf(plan, snap_ts, &rows).ok());
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].second, "one");
  EXPECT_EQ(rows[1].second, "two");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
  ebtree::ResetTimestampSourceForTest();
}
