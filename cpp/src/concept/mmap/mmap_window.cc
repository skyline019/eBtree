#include "ebtree/concept/mmap/mmap_window.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <filesystem>
#include <utility>

namespace ebtree {

MmapWindow::~MmapWindow() { Unmap(); }

Status MmapWindow::MapReadOnly(const std::string& path, uint64_t offset,
                               size_t length) {
  Unmap();
  if (length == 0) return Status::Ok();

#ifdef _WIN32
  HANDLE file = CreateFileA(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return Status::IoError("mmap open failed");
  }

  LARGE_INTEGER file_size{};
  if (!GetFileSizeEx(file, &file_size)) {
    CloseHandle(file);
    return Status::IoError("mmap file size failed");
  }
  if (offset >= static_cast<uint64_t>(file_size.QuadPart)) {
    CloseHandle(file);
    return Status::Ok();
  }

  const uint64_t avail =
      static_cast<uint64_t>(file_size.QuadPart) - offset;
  const size_t map_len =
      static_cast<size_t>(std::min<uint64_t>(avail, length));

  HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0,
                                        nullptr);
  if (!mapping) {
    CloseHandle(file);
    return Status::IoError("CreateFileMapping failed");
  }

  SYSTEM_INFO sys_info{};
  GetSystemInfo(&sys_info);
  const uint64_t gran = sys_info.dwAllocationGranularity;
  const uint64_t map_start = (offset / gran) * gran;
  const uint64_t delta = offset - map_start;

  uint8_t* view = static_cast<uint8_t*>(
      MapViewOfFile(mapping, FILE_MAP_READ,
                    static_cast<DWORD>(map_start >> 32),
                    static_cast<DWORD>(map_start & 0xFFFFFFFFu),
                    map_len + static_cast<size_t>(delta)));
  if (!view) {
    CloseHandle(mapping);
    CloseHandle(file);
    return Status::IoError("MapViewOfFile failed");
  }

  file_handle_ = file;
  mapping_handle_ = mapping;
  view_start_ = view;
  base_ = view + delta;
  mapped_size_ = map_len;
  file_offset_ = offset;
  return Status::Ok();
#else
  (void)path;
  (void)offset;
  (void)length;
  return Status::IoError("mmap not supported on this platform");
#endif
}

void MmapWindow::Unmap() {
#ifdef _WIN32
  if (view_start_) {
    UnmapViewOfFile(view_start_);
    view_start_ = nullptr;
  }
  if (mapping_handle_) {
    CloseHandle(static_cast<HANDLE>(mapping_handle_));
    mapping_handle_ = nullptr;
  }
  if (file_handle_) {
    CloseHandle(static_cast<HANDLE>(file_handle_));
    file_handle_ = nullptr;
  }
#endif
  base_ = nullptr;
  mapped_size_ = 0;
  file_offset_ = 0;
}

void swap(MmapWindow& a, MmapWindow& b) noexcept {
  using std::swap;
  swap(a.file_handle_, b.file_handle_);
  swap(a.mapping_handle_, b.mapping_handle_);
  swap(a.view_start_, b.view_start_);
  swap(a.base_, b.base_);
  swap(a.mapped_size_, b.mapped_size_);
  swap(a.file_offset_, b.file_offset_);
}

MmapWindowManager::MmapWindowManager(size_t window_size)
    : window_size_(window_size) {}

void MmapWindowManager::RefreshCachedFileSize() {
  cached_file_size_ = 0;
  const std::filesystem::path p(path_);
  if (std::filesystem::exists(p)) {
    cached_file_size_ = std::filesystem::file_size(p);
  }
}

Status MmapWindowManager::OpenReadOnly(const std::string& path) {
  Close();
  path_ = path;
  epoch_ = 0;
  pin_count_ = 0;
  active_window_start_ = 0;
  RefreshCachedFileSize();
  return RemapCurrentEpoch();
}

void MmapWindowManager::Close() {
  active_.Unmap();
  retired_.Unmap();
  path_.clear();
  epoch_ = 0;
  pin_count_ = 0;
  cached_file_size_ = 0;
  active_window_start_ = 0;
}

Status MmapWindowManager::RemapCurrentEpoch() {
  if (path_.empty()) return Status::Internal("mmap path not set");
  RefreshCachedFileSize();
  const size_t map_len =
      cached_file_size_ == 0
          ? 0
          : static_cast<size_t>(
                std::min<std::uintmax_t>(cached_file_size_, window_size_));
  active_window_start_ = 0;
  return active_.MapReadOnly(path_, 0, map_len);
}

Status MmapWindowManager::PinWindow(uint64_t offset, MmapView* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (path_.empty()) return Status::Internal("mmap not open");
  RefreshCachedFileSize();
  if (offset >= cached_file_size_) {
    *out = {};
    return Status::Ok();
  }
  const size_t avail = static_cast<size_t>(cached_file_size_ - offset);
  const size_t map_len = std::min<size_t>(window_size_, avail);
  if (active_.mapped() && offset >= active_window_start_ &&
      offset + map_len <= active_window_start_ + active_.view().size) {
    const size_t delta =
        static_cast<size_t>(offset - active_window_start_);
    *out = {active_.view().base + delta, map_len};
    ++pin_count_;
    return Status::Ok();
  }
  active_.Unmap();
  const Status st = active_.MapReadOnly(path_, offset, map_len);
  if (!st.ok()) return st;
  active_window_start_ = offset;
  ++pin_count_;
  *out = active_.view();
  return Status::Ok();
}

Status MmapWindowManager::Pin(MmapView* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (path_.empty()) return Status::Internal("mmap not open");
  if (!active_.mapped()) {
    const Status st = RemapCurrentEpoch();
    if (!st.ok()) return st;
  }
  ++pin_count_;
  *out = active_.view();
  return Status::Ok();
}

void MmapWindowManager::Unpin() {
  if (pin_count_ > 0) --pin_count_;
  if (pin_count_ == 0) {
    retired_.Unmap();
  }
}

Status MmapWindowManager::RotateEpoch() {
  retired_.Unmap();
  swap(active_, retired_);
  active_window_start_ = 0;
  ++epoch_;
  RefreshCachedFileSize();
  return RemapCurrentEpoch();
}

}  // namespace ebtree
