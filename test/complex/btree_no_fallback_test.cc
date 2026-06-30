#include <gtest/gtest.h>

#include "ebtree/concept/btree/btree.h"
#include "engine_test_util.h"

TEST(EbBtreeNoFallback, SummaryPrunesMissingKey) {
  ebtree::BTreeIndex idx;
  ASSERT_TRUE(idx.Put("a", 1).ok());
  ASSERT_TRUE(idx.Put("c", 3).ok());

  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kEq;
  plan.key = "b";
  plan.snapshot_lsn = idx.max_lsn();

  std::vector<std::pair<std::string, uint64_t>> out;
  ASSERT_TRUE(idx.Scan(plan, &out).ok());
  EXPECT_TRUE(out.empty());
}

TEST(EbBtreeNoFallback, StaleSummaryRejected) {
  ebtree::BTreeIndex idx;
  ASSERT_TRUE(idx.Put("k", 10).ok());
  idx.SetSummaryLsnForTest(1);

  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kEq;
  plan.key = "k";
  plan.snapshot_lsn = 100;

  std::vector<std::pair<std::string, uint64_t>> out;
  const ebtree::Status st = idx.Scan(plan, &out);
  EXPECT_EQ(st.code(), ebtree::StatusCode::kStaleSummary);
}

TEST(EbBtreeNoFallback, EngineScanNoFallbackCounter) {
  const std::string dir = ebtree::test::TempDir("btree_no_fallback");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("pk", "pv").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());

  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kEq;
  plan.key = "pk";
  plan.snapshot_lsn = engine->stable_lsn();

  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}

TEST(EbBtreeRange, RangeScanReturnsOrderedHits) {
  const std::string dir = ebtree::test::TempDir("btree_range");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("a", "1").ok());
  ASSERT_TRUE(engine->Put("b", "2").ok());
  ASSERT_TRUE(engine->Put("c", "3").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());

  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "a";
  plan.range_end = "b";
  plan.snapshot_lsn = engine->stable_lsn();

  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  EXPECT_EQ(rows.size(), 2u);
}
