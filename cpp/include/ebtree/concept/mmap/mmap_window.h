#pragma once

#include <cstdint>
#include <string>

#include "ebtree/common/status.h"

namespace ebtree {

struct MmapView {
  const uint8_t* base{nullptr};
  size_t size{0};
};

class MmapWindow {
 public:
  MmapWindow() = default;
  ~MmapWindow();

  MmapWindow(const MmapWindow&) = delete;
  MmapWindow& operator=(const MmapWindow&) = delete;

  Status MapReadOnly(const std::string& path, uint64_t offset, size_t length);
  void Unmap();

  MmapView view() const { return {base_, mapped_size_}; }
  uint64_t file_offset() const { return file_offset_; }
  bool mapped() const { return base_ != nullptr; }

  friend void swap(MmapWindow& a, MmapWindow& b) noexcept;

 private:
  void* file_handle_{nullptr};
  void* mapping_handle_{nullptr};
  uint8_t* view_start_{nullptr};
  uint8_t* base_{nullptr};
  size_t mapped_size_{0};
  uint64_t file_offset_{0};
};

class MmapWindowManager {
 public:
  explicit MmapWindowManager(size_t window_size = 1024 * 1024);

  Status OpenReadOnly(const std::string& path);
  void Close();

  Status Pin(MmapView* out);
  Status PinWindow(uint64_t offset, MmapView* out);
  void Unpin();
  Status RotateEpoch();

  size_t window_size() const { return window_size_; }

  const std::string& path() const { return path_; }
  uint32_t epoch() const { return epoch_; }
  int pin_count() const { return pin_count_; }

 private:
  Status RemapCurrentEpoch();
  void RefreshCachedFileSize();

  std::string path_;
  size_t window_size_{0};
  std::uintmax_t cached_file_size_{0};
  uint64_t active_window_start_{0};
  uint32_t epoch_{0};
  int pin_count_{0};
  MmapWindow active_;
  MmapWindow retired_;
};

}  // namespace ebtree
