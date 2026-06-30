#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "engine_test_util.h"

TEST(EbComplexConcurrentRead, ParallelGetWhileWriting) {
  const std::string dir = ebtree::test::TempDir("concurrent_read");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  for (int i = 0; i < 100; ++i) {
    const std::string key = "cr" + std::to_string(i);
    ASSERT_TRUE(engine->Put(key, "cv").ok());
  }
  std::atomic<bool> stop{false};
  std::atomic<int> errors{0};
  const int thread_count = 8;
  std::vector<std::thread> readers;
  for (int t = 0; t < thread_count; ++t) {
    readers.emplace_back([&]() {
      while (!stop) {
        for (int i = 0; i < 100; ++i) {
          const std::string key = "cr" + std::to_string(i);
          std::string value;
          if (!engine->Get(key, &value).ok()) ++errors;
        }
      }
    });
  }
  for (int i = 0; i < 50; ++i) {
  ASSERT_TRUE(engine->Put("crw" + std::to_string(i), "nw").ok());
  }
  stop = true;
  for (auto& th : readers) th.join();
  EXPECT_EQ(errors.load(), 0);
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}
