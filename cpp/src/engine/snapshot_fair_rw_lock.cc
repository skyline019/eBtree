#include "ebtree/engine/snapshot_fair_rw_lock.h"

namespace ebtree {

uint32_t SnapshotFairRwLock::ActiveSnapshotPins() const {
  if (!snapshot_pins_) return 0;
  return snapshot_pins_->load(std::memory_order_acquire);
}

void SnapshotFairRwLock::lock_shared() {
  std::unique_lock<std::mutex> lock(gate_);
  while (foreground_writer_ || background_writer_) {
    cv_.wait(lock);
  }
  ++readers_;
}

bool SnapshotFairRwLock::try_lock_shared() {
  std::unique_lock<std::mutex> lock(gate_);
  if (foreground_writer_ || background_writer_) return false;
  ++readers_;
  return true;
}

void SnapshotFairRwLock::unlock_shared() {
  std::unique_lock<std::mutex> lock(gate_);
  if (readers_ == 0) return;
  --readers_;
  if (readers_ == 0) cv_.notify_all();
}

void SnapshotFairRwLock::lock_append_shared() {
  std::unique_lock<std::mutex> lock(gate_);
  while (foreground_writer_ || background_writer_) {
    cv_.wait(lock);
  }
  ++appenders_;
}

void SnapshotFairRwLock::unlock_append_shared() {
  std::unique_lock<std::mutex> lock(gate_);
  if (appenders_ == 0) return;
  --appenders_;
  cv_.notify_all();
}

void SnapshotFairRwLock::lock() {
  std::unique_lock<std::mutex> lock(gate_);
  ++foreground_waiting_;
  while (readers_ > 0 || appenders_ > 0 || foreground_writer_ ||
         background_writer_) {
    cv_.wait(lock);
  }
  --foreground_waiting_;
  foreground_writer_ = true;
}

bool SnapshotFairRwLock::try_lock() {
  std::unique_lock<std::mutex> lock(gate_);
  if (readers_ > 0 || appenders_ > 0 || foreground_writer_ ||
      background_writer_) {
    return false;
  }
  foreground_writer_ = true;
  return true;
}

void SnapshotFairRwLock::unlock() {
  std::unique_lock<std::mutex> lock(gate_);
  foreground_writer_ = false;
  cv_.notify_all();
}

bool SnapshotFairRwLock::try_lock_background_exclusive() {
  std::unique_lock<std::mutex> lock(gate_);
  if (ActiveSnapshotPins() > 0) return false;
  if (readers_ > 0 || appenders_ > 0 || foreground_writer_ ||
      background_writer_ || foreground_waiting_ > 0) {
    return false;
  }
  background_writer_ = true;
  return true;
}

void SnapshotFairRwLock::unlock_background_exclusive() {
  std::unique_lock<std::mutex> lock(gate_);
  background_writer_ = false;
  cv_.notify_all();
}

void SnapshotFairRwLock::notify_pins_released() {
  std::lock_guard<std::mutex> lock(gate_);
  cv_.notify_all();
}

}  // namespace ebtree
