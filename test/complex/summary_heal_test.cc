#include <gtest/gtest.h>

#include "ebtree/concept/btree/btree.h"
#include "engine_test_util.h"

TEST(EbSummaryHeal, ScanRepairsStaleSummary) {
  const std::string dir = ebtree::test::TempDir("summary_heal");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("heal_k", "heal_v").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  const uint64_t before_recovery = engine->stats().recovery_total;
  engine->btree()->SetSummaryLsnForTest(0);

  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kEq;
  plan.key = "heal_k";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].second, "heal_v");
  EXPECT_GE(engine->stats().summary_repair_total, 1u);
  EXPECT_EQ(engine->stats().recovery_total, before_recovery);
}

TEST(EbSummaryHeal, GetRepairsStaleSummary) {
  const std::string dir = ebtree::test::TempDir("summary_heal_get");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("gk", "gv").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  engine->btree()->SetSummaryLsnForTest(0);
  std::string value;
  ASSERT_TRUE(engine->Get("gk", &value).ok());
  EXPECT_EQ(value, "gv");
  EXPECT_GE(engine->stats().summary_repair_total, 1u);
}
