#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "ebtree/engine/snapshot_fair_rw_lock.h"

namespace ebtree {
namespace {

TEST(Tsl3Lock, ReadersConcurrentWithAppenders) {
  SnapshotFairRwLock lock;
  std::atomic<int> readers_done{0};
  std::atomic<bool> stop{false};
  std::atomic<bool> reader_ready{false};

  std::thread reader([&] {
    reader_ready.store(true, std::memory_order_release);
    while (!stop.load(std::memory_order_acquire)) {
      lock.lock_shared();
      lock.unlock_shared();
      readers_done.fetch_add(1, std::memory_order_relaxed);
    }
  });

  while (!reader_ready.load(std::memory_order_acquire)) {
  }

  for (int i = 0; i < 200; ++i) {
    lock.lock_append_shared();
    lock.unlock_append_shared();
  }

  stop.store(true, std::memory_order_release);
  reader.join();
  EXPECT_GT(readers_done.load(), 0);
}

TEST(Tsl3Lock, BackgroundYieldsWhenReadersActive) {
  SnapshotFairRwLock lock;
  lock.lock_shared();
  EXPECT_FALSE(lock.try_lock_background_exclusive());
  lock.unlock_shared();
  EXPECT_TRUE(lock.try_lock_background_exclusive());
  lock.unlock_background_exclusive();
}

}  // namespace
}  // namespace ebtree
