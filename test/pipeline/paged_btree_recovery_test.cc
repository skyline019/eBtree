#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "ebtree/concept/page/page_format.h"

TEST(EbPipelinePagedBtree, FlushReopenScanNoFallback) {
  const std::string dir = ebtree::test::TempDir("paged_recovery");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 50; ++i) {
      const std::string key = "pg" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, "pv").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "pg0";
  plan.range_end = "pg99";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  EXPECT_EQ(rows.size(), 50u);
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
  EXPECT_GE(engine->loaded_superblock().critical.active_root,
            ebtree::kPageFileHeaderSize);
}
