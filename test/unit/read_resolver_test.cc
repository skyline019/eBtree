#include <gtest/gtest.h>

#include "engine_test_util.h"

namespace ebtree {
namespace test {
namespace {

TEST(ReadResolverTest, GetRecordsCommittedTierAfterCheckpoint) {
  const std::string dir = TempDir("read_resolver_committed");
  auto engine = OpenEngine(dir, DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("rk", "rv").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  std::string value;
  ASSERT_TRUE(engine->Get("rk", &value).ok());
  EXPECT_EQ(value, "rv");
  const EngineStats& stats = engine->stats();
  EXPECT_GT(stats.read_tier_hits[static_cast<size_t>(ReadTier::kCommitted)], 0u);
  EXPECT_EQ(stats.unexpected_path_total, 0u);
}

TEST(ReadResolverTest, ScanUsesCommittedDirectTier) {
  const std::string dir = TempDir("read_resolver_scan_direct");
  EngineOptions opts = EngineOptions::EnterpriseDefaults(dir);
  opts.lazy_committed_load = false;
  auto engine = OpenEngineWithOptions(dir, opts);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("sk", "sv").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  TypedPlan plan;
  plan.op = PredicateOp::kEq;
  plan.key = "sk";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].second, "sv");
  const EngineStats& stats = engine->stats();
  EXPECT_GT(stats.read_tier_hits[static_cast<size_t>(ReadTier::kCommittedDirectScan)],
            0u);
  EXPECT_EQ(stats.unexpected_path_total, 0u);
}

TEST(ReadResolverTest, FlashbackRecordsTLogTier) {
  const std::string dir = TempDir("read_resolver_flashback");
  auto engine = OpenEngine(dir, DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("fb", "fbv").ok());
  SetTimestampSourceForTest([]() { return 8000u; });
  ASSERT_TRUE(engine->Checkpoint().ok());
  std::string value;
  ASSERT_TRUE(engine->GetAsOf("fb", 8000u, &value).ok());
  EXPECT_EQ(value, "fbv");
  const EngineStats& stats = engine->stats();
  EXPECT_GT(stats.read_tier_hits[static_cast<size_t>(ReadTier::kTLogFlashback)], 0u);
  EXPECT_EQ(stats.unexpected_path_total, 0u);
  ResetTimestampSourceForTest();
}

TEST(ReadResolverTest, SequentialScansSameThread) {
  const std::string dir = TempDir("read_resolver_seq_scan");
  auto engine = OpenEngine(dir, DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(engine->Put("k" + std::to_string(i), "v").ok());
  }
  ASSERT_TRUE(engine->Checkpoint().ok());

  TypedPlan plan1;
  plan1.op = PredicateOp::kRange;
  plan1.key = "k0";
  plan1.range_end = "k9";
  plan1.snapshot_lsn = engine->stable_lsn();

  TypedPlan plan2;
  plan2.op = PredicateOp::kEq;
  plan2.key = "k2";
  plan2.snapshot_lsn = engine->stable_lsn();

  std::vector<std::pair<std::string, std::string>> rows1;
  std::vector<std::pair<std::string, std::string>> rows2;
  ASSERT_TRUE(engine->Scan(plan1, &rows1).ok());
  ASSERT_TRUE(engine->Scan(plan2, &rows2).ok());
  EXPECT_EQ(rows1.size(), 4u);
  EXPECT_EQ(rows2.size(), 1u);
  EXPECT_EQ(rows2[0].second, "v");
}

}  // namespace
}  // namespace test
}  // namespace ebtree
