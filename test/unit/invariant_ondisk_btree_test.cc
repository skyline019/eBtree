#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "ebtree/concept/btree/btree.h"
#include "ebtree/engine/engine.h"

TEST(EbInvariantOndiskBtree, OpenLoadsRootOnly) {
  const std::string dir = ebtree::test::TempDir("ondisk_open");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 1000; ++i) {
      const std::string key = "ok" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, "ov").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  EXPECT_TRUE(engine->btree()->on_disk_mode());
  EXPECT_EQ(1u, engine->btree()->pages_touched());
  std::string value;
  ASSERT_TRUE(engine->Get("ok500", &value).ok());
  EXPECT_EQ(value, "ov");
}

TEST(EbInvariantOndiskBtree, RangeScanUsesDiskPruning) {
  const std::string dir = ebtree::test::TempDir("ondisk_scan");
  ebtree::BTreeIndex btree(dir + "/shard0.pages");
  for (int i = 0; i < 80; ++i) {
    const std::string key = "sk" + std::to_string(i);
    ASSERT_TRUE(btree.Put(key, static_cast<uint64_t>(i + 1)).ok());
  }
  std::map<std::string, uint64_t> full;
  for (int i = 0; i < 80; ++i) {
    full["sk" + std::to_string(i)] = static_cast<uint64_t>(i + 1);
  }
  uint64_t root = 0;
  ASSERT_TRUE(btree.PersistRootFromMap(full, &root).ok());
  ebtree::BTreeIndex loaded(dir + "/shard0.pages");
  ASSERT_TRUE(loaded.LoadRoot(root).ok());
  EXPECT_EQ(1u, loaded.pages_touched());
  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "sk10";
  plan.range_end = "sk19";
  std::vector<std::pair<std::string, uint64_t>> hits;
  const uint64_t touched_before = loaded.pages_touched();
  ASSERT_TRUE(loaded.Scan(plan, &hits).ok());
  EXPECT_EQ(10u, hits.size());
  EXPECT_LT(loaded.pages_touched() - touched_before, 80u);
}

TEST(EbInvariantOndiskBtree, PersistFromCommittedPreservesKeys) {
  const std::string dir = ebtree::test::TempDir("ondisk_persist");
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("pk1", "pv1").ok());
  ASSERT_TRUE(engine->Put("pk2", "pv2").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  std::string value;
  ASSERT_TRUE(engine->Get("pk1", &value).ok());
  EXPECT_EQ(value, "pv1");
}

TEST(EbForbiddenEnforcement, OpenDoesNotEagerLoadAllPages) {
  const std::string dir = ebtree::test::TempDir("forbidden_ondisk_open");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 500; ++i) {
      ASSERT_TRUE(engine->Put("eo" + std::to_string(i), "ev").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  EXPECT_LE(engine->btree()->pages_touched(), 2u);
}

TEST(EbForbiddenEnforcement, LazyGetNoWalFullScan) {
  const std::string dir = ebtree::test::TempDir("forbidden_wal_scan");
  {
    auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 200; ++i) {
      ASSERT_TRUE(engine->Put("ws" + std::to_string(i), "wv").ok());
    }
  }
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.recovery_strategy = ebtree::RecoveryStrategy::kFastOpen;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  std::string value;
  ASSERT_TRUE(engine->Get("ws42", &value).ok());
  EXPECT_EQ(value, "wv");
  EXPECT_EQ(engine->stats().wal_full_scan_total, 0u);
}

TEST(EbInvariantOndiskBtree, HistogramSummaryType) {
  EXPECT_EQ(ebtree::kSummaryTypeHistogram, 2u);
}
