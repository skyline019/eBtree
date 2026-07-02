#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace ebtree {

// Snapshot-Priority Fair RW lock (SPF-RW / TSL-3):
// L0 append lane can run concurrently with L1 readers.
// L2 structural (foreground exclusive / background exclusive) excludes readers.
class SnapshotFairRwLock {
 public:
  using Tsl3Lock = SnapshotFairRwLock;

  void set_snapshot_pin_counter(std::atomic<uint32_t>* pins) {
    snapshot_pins_ = pins;
  }

  void lock_shared();
  bool try_lock_shared();
  void unlock_shared();

  void lock_append_shared();
  void unlock_append_shared();

  void lock();
  bool try_lock();
  void unlock();

  bool try_lock_background_exclusive();
  void unlock_background_exclusive();

  void notify_pins_released();

 private:
  uint32_t ActiveSnapshotPins() const;

  mutable std::mutex gate_;
  std::condition_variable cv_;
  uint32_t readers_{0};
  uint32_t appenders_{0};
  bool foreground_writer_{false};
  uint32_t foreground_waiting_{0};
  bool background_writer_{false};
  std::atomic<uint32_t>* snapshot_pins_{nullptr};
};

}  // namespace ebtree
