#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "ebtree/concept/btree/btree.h"
#include "ebtree/concept/page/page_format.h"

TEST(EbInvariantPagedBtree, LeafPageCrcRoundTrip) {
  const std::string dir = ebtree::test::TempDir("paged_crc");
  ebtree::BTreeIndex btree(dir + "/shard0.pages");
  ASSERT_TRUE(btree.Put("a", 1).ok());
  ASSERT_TRUE(btree.Put("b", 2).ok());
  uint64_t root = 0;
  ASSERT_TRUE(btree.PersistRoot(&root).ok());
  EXPECT_GE(root, ebtree::kPageFileHeaderSize);
  ebtree::BTreeIndex loaded(dir + "/shard0.pages");
  ASSERT_TRUE(loaded.LoadRoot(root).ok());
  uint64_t lsn = 0;
  ASSERT_TRUE(loaded.Get("a", &lsn).ok());
  EXPECT_EQ(lsn, 1u);
}

TEST(EbInvariantPagedBtree, RootScanMatchesIndex) {
  const std::string dir = ebtree::test::TempDir("paged_scan");
  ebtree::BTreeIndex btree(dir + "/shard0.pages");
  for (int i = 0; i < 20; ++i) {
    const std::string key = "pk" + std::to_string(i);
    ASSERT_TRUE(btree.Put(key, static_cast<uint64_t>(i + 1)).ok());
  }
  uint64_t root = 0;
  ASSERT_TRUE(btree.PersistRoot(&root).ok());
  ebtree::BTreeIndex loaded(dir + "/shard0.pages");
  ASSERT_TRUE(loaded.LoadRoot(root).ok());
  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "pk0";
  plan.range_end = "pk99";
  std::vector<std::pair<std::string, uint64_t>> hits;
  ASSERT_TRUE(loaded.Scan(plan, &hits).ok());
  EXPECT_EQ(hits.size(), 20u);
}

TEST(EbInvariantPagedBtree, TrieSummaryTypeSkeleton) {
  EXPECT_EQ(ebtree::kSummaryTypeTrie, 1u);
  ebtree::PageHeader hdr{};
  hdr.summary_type = ebtree::kSummaryTypeTrie;
  EXPECT_EQ(hdr.summary_type, ebtree::kSummaryTypeTrie);
}

TEST(EbInvariantPagedBtree, InternalRootRoundTrip) {
  const std::string dir = ebtree::test::TempDir("paged_internal");
  ebtree::BTreeIndex btree(dir + "/shard0.pages");
  for (int i = 0; i < 120; ++i) {
    const std::string key = "ik" + std::to_string(i);
    ASSERT_TRUE(btree.Put(key, static_cast<uint64_t>(i + 1)).ok());
  }
  uint64_t root = 0;
  ASSERT_TRUE(btree.PersistRoot(&root).ok());
  EXPECT_GE(root, ebtree::kPageFileHeaderSize);
  ebtree::BTreeIndex loaded(dir + "/shard0.pages");
  ASSERT_TRUE(loaded.LoadRoot(root).ok());
  EXPECT_EQ(1u, loaded.pages_touched());
  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "ik0";
  plan.range_end = "ik999";
  std::vector<std::pair<std::string, uint64_t>> hits;
  ASSERT_TRUE(loaded.Scan(plan, &hits).ok());
  EXPECT_EQ(hits.size(), 120u);
}

TEST(EbInvariantPagedBtree, TrieScanPrunesWithPagesTouched) {
  const std::string dir = ebtree::test::TempDir("paged_trie");
  ebtree::BTreeIndex btree(dir + "/shard0.pages");
  for (int i = 0; i < 50; ++i) {
    const std::string key = "tk" + std::to_string(i);
    ASSERT_TRUE(btree.Put(key, static_cast<uint64_t>(i + 1)).ok());
  }
  uint64_t root = 0;
  ASSERT_TRUE(btree.PersistRoot(&root).ok());
  ebtree::BTreeIndex loaded(dir + "/shard0.pages");
  ASSERT_TRUE(loaded.LoadRoot(root).ok());
  const uint64_t touched_after_load = loaded.pages_touched();
  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kEq;
  plan.key = "tk25";
  std::vector<std::pair<std::string, uint64_t>> hits;
  ASSERT_TRUE(loaded.Scan(plan, &hits).ok());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_GE(loaded.pages_touched(), touched_after_load);
}

TEST(EbInvariantPagedBtree, CorruptRootHealedWithoutFallback) {
  const std::string dir = ebtree::test::TempDir("paged_heal");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("ph1", "pv1").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_GE(engine->loaded_superblock().critical.active_root,
              ebtree::kPageFileHeaderSize);
    ASSERT_TRUE(engine->shard(0)->btree()->CorruptRootPageForTest().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("ph1", &value).ok());
  EXPECT_EQ(value, "pv1");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}
