#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "engine_test_util.h"

TEST(EbInvariantConcurrency, ConcurrentReadDoesNotDeferWal) {
  const std::string dir = ebtree::test::TempDir("conc_wal");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("cw1", "v1").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->Put("cw2", "v2").ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  EXPECT_TRUE(engine->wal_replay_pending());
  std::atomic<bool> stop{false};
  std::thread reader([&]() {
    while (!stop) {
      std::string value;
      (void)engine->Get("cw1", &value);
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stop = true;
  reader.join();
  EXPECT_TRUE(engine->wal_replay_pending());
  EXPECT_EQ(engine->stats().wal_replay_deferred_total, 0u);
}

TEST(EbInvariantConcurrency, ConcurrentReadsConsistent) {
  const std::string dir = ebtree::test::TempDir("conc_consistent");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("cc", "cv").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  std::atomic<int> bad{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 16; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < 100; ++j) {
        std::string value;
        if (!engine->Get("cc", &value).ok() || value != "cv") ++bad;
      }
    });
  }
  for (auto& t : threads) t.join();
  EXPECT_EQ(bad.load(), 0);
}
