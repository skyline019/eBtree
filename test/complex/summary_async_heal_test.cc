#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "engine_test_util.h"

namespace ebtree {
namespace test {
namespace {

EngineOptions AsyncSummaryOptions(const std::string& dir) {
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  opts.background_summary_validate = true;
  return opts;
}

}  // namespace
}  // namespace test
}  // namespace ebtree

TEST(EbSummaryAsyncHeal, BackgroundValidatorRepairsStaleSummary) {
  const std::string dir = ebtree::test::TempDir("summary_async_heal");
  ebtree::EngineOptions opts = ebtree::test::AsyncSummaryOptions(dir);
  auto engine = ebtree::test::OpenEngineWithOptions(dir, opts);
  ASSERT_NE(engine, nullptr);
  const uint64_t before_recovery = engine->stats().recovery_total;
  ASSERT_TRUE(engine->Put("async_k", "async_v").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  engine->btree()->SetSummaryLsnForTest(0);

  for (int i = 0; i < 200; ++i) {
    if (engine->stats().summary_repair_total >= 1) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  EXPECT_GE(engine->stats().summary_repair_total, 1u);
  EXPECT_EQ(engine->stats().recovery_total, before_recovery);

  std::string value;
  ASSERT_TRUE(engine->Get("async_k", &value).ok());
  EXPECT_EQ(value, "async_v");
}

TEST(EbSummaryAsyncHeal, ConcurrentReadDuringValidation) {
  const std::string dir = ebtree::test::TempDir("summary_async_read");
  ebtree::EngineOptions opts = ebtree::test::AsyncSummaryOptions(dir);
  auto engine = ebtree::test::OpenEngineWithOptions(dir, opts);
  ASSERT_NE(engine, nullptr);
  for (int i = 0; i < 50; ++i) {
    const std::string key = "ar" + std::to_string(i);
    ASSERT_TRUE(engine->Put(key, "v").ok());
  }
  ASSERT_TRUE(engine->Checkpoint().ok());
  engine->btree()->SetSummaryLsnForTest(0);

  std::atomic<bool> stop{false};
  std::atomic<int> errors{0};
  std::thread reader([&]() {
    while (!stop) {
      for (int i = 0; i < 50; ++i) {
        std::string value;
        if (!engine->Get("ar" + std::to_string(i), &value).ok()) ++errors;
      }
    }
  });
  for (int i = 0; i < 200; ++i) {
    if (engine->stats().summary_repair_total >= 1) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  stop = true;
  reader.join();
  EXPECT_EQ(errors.load(), 0);
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}
