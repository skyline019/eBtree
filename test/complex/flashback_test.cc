#include <gtest/gtest.h>

#include "ebtree/concept/tlog/tlog.h"
#include "engine_test_util.h"

TEST(EbComplexFlashback, GetAsOfAtCheckpoints) {
  const std::string dir = ebtree::test::TempDir("flashback_get");
  uint32_t ts0 = 1000;
  uint32_t ts1 = 2000;
  uint32_t ts2 = 3000;
  ebtree::SetTimestampSourceForTest([&]() -> uint32_t {
    static int n = 0;
    if (n == 0) {
      ++n;
      return ts0;
    }
    if (n == 1) {
      ++n;
      return ts1;
    }
    return ts2;
  });

  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("state", "v0").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  ASSERT_TRUE(engine->Put("state", "v1").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  ASSERT_TRUE(engine->Put("state", "v2").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());

  std::string value;
  ASSERT_TRUE(engine->GetAsOf("state", ts0, &value).ok());
  EXPECT_EQ(value, "v0");
  ASSERT_TRUE(engine->GetAsOf("state", ts1, &value).ok());
  EXPECT_EQ(value, "v1");
  ASSERT_TRUE(engine->GetAsOf("state", ts2, &value).ok());
  EXPECT_EQ(value, "v2");
  EXPECT_TRUE(ebtree::test::IsNotFound(engine->GetAsOf("state", ts0 - 1, &value)));
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);

  ebtree::ResetTimestampSourceForTest();
}

TEST(EbComplexFlashback, ScanAsOfRange) {
  const std::string dir = ebtree::test::TempDir("flashback_scan");
  uint32_t ts0 = 5000;
  uint32_t ts1 = 6000;
  ebtree::SetTimestampSourceForTest([&]() -> uint32_t {
    static int n = 0;
    if (n == 0) {
      ++n;
      return ts0;
    }
    return ts1;
  });

  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("a", "1").ok());
  ASSERT_TRUE(engine->Put("b", "2").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  ASSERT_TRUE(engine->Put("a", "9").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());

  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "a";
  plan.range_end = "c";
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->ScanAsOf(plan, ts0, &rows).ok());
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].second, "1");
  EXPECT_EQ(rows[1].second, "2");

  rows.clear();
  ASSERT_TRUE(engine->ScanAsOf(plan, ts1, &rows).ok());
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].second, "9");
  EXPECT_EQ(rows[1].second, "2");

  ebtree::ResetTimestampSourceForTest();
}

namespace {

ebtree::EngineOptions MultiShardFlashbackOpts(const std::string& dir) {
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.sync_on_commit = true;
  opts.shard_count = 4;
  return opts;
}

}  // namespace

TEST(EbComplexFlashback, MultishardGetAsOfAtCheckpoints) {
  const std::string dir = ebtree::test::TempDir("flashback_ms_get");
  const auto opts = MultiShardFlashbackOpts(dir);
  uint32_t ts0 = 11000;
  uint32_t ts1 = 12000;
  uint32_t ts2 = 13000;
  ebtree::SetTimestampSourceForTest([&]() -> uint32_t {
    static int n = 0;
    if (n == 0) {
      ++n;
      return ts0;
    }
    if (n == 1) {
      ++n;
      return ts1;
    }
    return ts2;
  });

  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("ms_a", "v0").ok());
    ASSERT_TRUE(engine->Put("ms_b", "w0").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->Put("ms_a", "v1").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->Put("ms_b", "w2").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
  }

  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->GetAsOf("ms_a", ts0, &value).ok());
  EXPECT_EQ(value, "v0");
  ASSERT_TRUE(engine->GetAsOf("ms_b", ts0, &value).ok());
  EXPECT_EQ(value, "w0");
  ASSERT_TRUE(engine->GetAsOf("ms_a", ts1, &value).ok());
  EXPECT_EQ(value, "v1");
  ASSERT_TRUE(engine->GetAsOf("ms_b", ts2, &value).ok());
  EXPECT_EQ(value, "w2");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);

  ebtree::ResetTimestampSourceForTest();
}

TEST(EbComplexFlashback, MultishardScanAsOfMerge) {
  const std::string dir = ebtree::test::TempDir("flashback_ms_scan");
  const auto opts = MultiShardFlashbackOpts(dir);
  uint32_t ts0 = 14000;
  uint32_t ts1 = 15000;
  ebtree::SetTimestampSourceForTest([&]() -> uint32_t {
    static int n = 0;
    if (n == 0) {
      ++n;
      return ts0;
    }
    return ts1;
  });

  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 8; ++i) {
      const std::string key = "scan" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, "v" + std::to_string(i)).ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
    for (int i = 0; i < 4; ++i) {
      const std::string key = "scan" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, "u" + std::to_string(i)).ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }

  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_NE(engine, nullptr);

  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kRange;
  plan.key = "scan0";
  plan.range_end = "scan9";
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->ScanAsOf(plan, ts0, &rows).ok());
  ASSERT_EQ(rows.size(), 8u);
  for (size_t i = 1; i < rows.size(); ++i) {
    EXPECT_LT(rows[i - 1].first, rows[i].first);
  }
  EXPECT_EQ(rows[0].second, "v0");

  rows.clear();
  ASSERT_TRUE(engine->ScanAsOf(plan, ts1, &rows).ok());
  ASSERT_EQ(rows.size(), 8u);
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(rows[static_cast<size_t>(i)].second, "u" + std::to_string(i));
  }
  for (size_t i = 4; i < rows.size(); ++i) {
    EXPECT_EQ(rows[i].second, "v" + std::to_string(i));
  }
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);

  ebtree::ResetTimestampSourceForTest();
}
