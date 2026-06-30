#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"

TEST(EbLazyCommittedGet, CheckpointReopenGetAll) {
  const std::string dir = ebtree::test::TempDir("lazy_committed");
  ebtree::EngineOptions write_opts;
  write_opts.path = dir;
  write_opts.durability = ebtree::DurabilityClass::kSync;
  write_opts.sync_on_commit = true;
  {
    auto engine = ebtree::test::OpenEngineWithOptions(dir, write_opts);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 10000; ++i) {
      ASSERT_TRUE(engine->Put("lc" + std::to_string(i), "payload").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }

  ebtree::EngineOptions opts = write_opts;
  opts.lazy_committed_load = true;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  EXPECT_TRUE(engine->shard(0)->committed().empty());
  EXPECT_TRUE(engine->btree()->on_disk_mode());

  for (int i = 0; i < 10000; ++i) {
    std::string value;
    ASSERT_TRUE(engine->Get("lc" + std::to_string(i), &value).ok()) << i;
    EXPECT_EQ(value, "payload");
  }
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
  EXPECT_EQ(engine->stats().wal_full_scan_total, 0u);
}

TEST(EbLazyCommittedGet, ScanRangeAfterReopen) {
  const std::string dir = ebtree::test::TempDir("lazy_committed_scan");
  ebtree::EngineOptions write_opts;
  write_opts.path = dir;
  write_opts.durability = ebtree::DurabilityClass::kSync;
  write_opts.sync_on_commit = true;
  {
    auto engine = ebtree::test::OpenEngineWithOptions(dir, write_opts);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 500; ++i) {
      char key[16];
      std::snprintf(key, sizeof(key), "ls%03d", i);
      ASSERT_TRUE(engine->Put(key, "v").ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }

  ebtree::EngineOptions opts = write_opts;
  opts.lazy_committed_load = true;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());

  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "ls000";
  plan.range_end = "ls499";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  EXPECT_EQ(rows.size(), 500u);
}
