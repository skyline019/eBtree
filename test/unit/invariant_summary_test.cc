#include <gtest/gtest.h>

#include "engine_test_util.h"

TEST(EbInvariantSummary, RepairDoesNotTriggerRecovery) {
  const std::string dir = ebtree::test::TempDir("inv_nf2b");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("nf2b", "v").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  const uint64_t before = engine->stats().recovery_total;
  engine->btree()->SetSummaryLsnForTest(0);
  std::string value;
  ASSERT_TRUE(engine->Get("nf2b", &value).ok());
  EXPECT_EQ(engine->stats().recovery_total, before);
  EXPECT_GE(engine->stats().summary_repair_total, 1u);
}
