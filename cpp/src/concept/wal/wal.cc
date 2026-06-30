#include "ebtree/concept/wal/wal.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "ebtree/common/crc32.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <malloc.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ebtree {

namespace {

constexpr size_t kSectorSize = 4096;
constexpr size_t kStagingReserve = 512 * 1024;

void AppendBytes(std::vector<char>* buf, size_t* used, const void* data,
                 size_t len) {
  const auto* bytes = static_cast<const char*>(data);
  if (*used + len > buf->size()) {
    buf->resize(std::max(buf->size() * 2, *used + len));
  }
  std::memcpy(buf->data() + *used, bytes, len);
  *used += len;
}

#if defined(_WIN32)
void* OpenSyncHandle(const std::string& path, bool write_through,
                     bool no_buffering) {
  DWORD flags = FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
  if (write_through) flags |= FILE_FLAG_WRITE_THROUGH;
  if (no_buffering) flags |= FILE_FLAG_NO_BUFFERING;
  return CreateFileA(path.c_str(), GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                     flags, nullptr);
}

void* OpenPartialHandle(const std::string& path) {
  return CreateFileA(path.c_str(), GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_FLAG_WRITE_THROUGH,
                     nullptr);
}

char* AllocAligned(size_t size) {
  return static_cast<char*>(_aligned_malloc(size, kSectorSize));
}
#endif

}  // namespace

WalWriter::WalWriter(std::string path) : path_(std::move(path)) {
  std::filesystem::create_directories(
      std::filesystem::path(path_).parent_path());
  file_.open(path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
  if (!file_) {
    std::ofstream create(path_, std::ios::binary);
    create.close();
    file_.open(path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
  }
#if defined(_WIN32)
  sync_handle_ = OpenSyncHandle(path_, false, false);
#else
  sync_fd_ = ::open(path_.c_str(), O_RDWR);
#endif
  (void)RebuildKeyIndex();
}

WalWriter::~WalWriter() {
#if defined(_WIN32)
  if (aligned_io_buf_) {
    _aligned_free(aligned_io_buf_);
  }
  if (sync_handle_ && sync_handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(static_cast<HANDLE>(sync_handle_));
  }
  if (partial_handle_ && partial_handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(static_cast<HANDLE>(partial_handle_));
  }
#else
  if (sync_fd_ >= 0) {
    ::close(sync_fd_);
  }
#endif
}

void WalWriter::SetWriteThrough(bool enable) {
  std::lock_guard<std::mutex> lock(mu_);
  if (write_through_ == enable) return;
  write_through_ = enable;
#if defined(_WIN32)
  no_buffering_ = enable;
  if (sync_handle_ && sync_handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(static_cast<HANDLE>(sync_handle_));
    sync_handle_ = nullptr;
  }
  sync_handle_ = OpenSyncHandle(path_, enable, enable);
  if (enable) {
    if (partial_handle_ && partial_handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(static_cast<HANDLE>(partial_handle_));
    }
    partial_handle_ = OpenPartialHandle(path_);
    staging_buf_.resize(kStagingReserve);
    staging_used_ = 0;
    if (!aligned_io_buf_) {
      aligned_io_cap_ = kStagingReserve;
      aligned_io_buf_ = AllocAligned(aligned_io_cap_);
    }
    if (file_offset_ % kSectorSize != 0) {
      const uint64_t aligned =
          ((file_offset_ + kSectorSize - 1) / kSectorSize) * kSectorSize;
      EnsureFileCapacityLocked(aligned);
      file_offset_ = aligned;
      file_.clear();
      file_.seekp(static_cast<std::streamoff>(file_offset_));
    }
    EnsureFileCapacityLocked(64ULL * 1024 * 1024);
  }
#endif
}

size_t WalWriter::UnflushedBytes() const {
  std::lock_guard<std::mutex> lock(mu_);
  return staging_used_;
}

void WalWriter::EnsureStagingCapacity(size_t extra) {
  if (staging_used_ + extra <= staging_buf_.size()) return;
  staging_buf_.resize(std::max(staging_buf_.size() * 2, staging_used_ + extra));
}

void WalWriter::EnsureFileCapacityLocked(uint64_t end_offset) {
#if defined(_WIN32)
  if (!write_through_ || !sync_handle_ || sync_handle_ == INVALID_HANDLE_VALUE) {
    return;
  }
  const uint64_t aligned_end =
      ((end_offset + kSectorSize - 1) / kSectorSize) * kSectorSize;
  if (aligned_end <= durable_capacity_) return;
  HANDLE handle = static_cast<HANDLE>(sync_handle_);
  LARGE_INTEGER pos{};
  pos.QuadPart = static_cast<LONGLONG>(aligned_end);
  SetFilePointerEx(handle, pos, nullptr, FILE_BEGIN);
  SetEndOfFile(handle);
  durable_capacity_ = aligned_end;
#endif
}

Status WalWriter::RebuildKeyIndex() {
  max_lsn_ = 0;
  staging_buf_.clear();
  staging_used_ = 0;
  file_.clear();
  file_.seekg(0, std::ios::end);
  const auto end = file_.tellg();
  file_offset_ = end > 0 ? static_cast<uint64_t>(end) : 0;
  durable_capacity_ = ((file_offset_ + kSectorSize - 1) / kSectorSize) * kSectorSize;
  if (end > 0) {
    file_.seekg(0);
    while (file_.tellg() < end) {
      const auto offset = static_cast<uint64_t>(file_.tellg());
      WalRecordHeader hdr{};
      file_.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
      if (!file_) break;
      std::string key(hdr.key_len, '\0');
      std::string value(hdr.value_len, '\0');
      if (hdr.key_len) file_.read(key.data(), hdr.key_len);
      if (hdr.value_len) file_.read(value.data(), hdr.value_len);
      max_lsn_ = std::max(max_lsn_, hdr.lsn);
      key_index_.Update(offset, key, hdr.lsn);
    }
    file_.clear();
    file_.seekp(0, std::ios::end);
  } else {
    key_index_.Clear();
  }
  pending_index_.clear();
  return Status::Ok();
}

Status WalWriter::AppendRecord(const WalRecordHeader& hdr_in,
                               const std::string& key,
                               const std::string& value) {
  const size_t record_size =
      sizeof(WalRecordHeader) + key.size() + value.size();
  const auto offset = write_through_
                          ? file_offset_ + staging_used_
                          : static_cast<uint64_t>(file_.tellp());
  WalRecordHeader hdr = hdr_in;
  hdr.record_crc = Crc32(&hdr, sizeof(hdr) - sizeof(hdr.record_crc));
  if (write_through_) {
    EnsureStagingCapacity(record_size);
    AppendBytes(&staging_buf_, &staging_used_, &hdr, sizeof(hdr));
    if (!key.empty()) AppendBytes(&staging_buf_, &staging_used_, key.data(), key.size());
    if (!value.empty()) {
      AppendBytes(&staging_buf_, &staging_used_, value.data(), value.size());
    }
  } else {
    file_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (!key.empty()) {
      file_.write(key.data(), static_cast<std::streamsize>(key.size()));
    }
    if (!value.empty()) {
      file_.write(value.data(), static_cast<std::streamsize>(value.size()));
    }
    if (!file_) {
      return Status::IoError("wal append failed");
    }
  }
  pending_index_.push_back({offset, key, hdr.lsn});
  return Status::Ok();
}

Status WalWriter::Append(WalOp op, const std::string& key,
                         const std::string& value, uint64_t* out_lsn) {
  std::lock_guard<std::mutex> lock(mu_);
  WalRecordHeader hdr{};
  hdr.lsn = ++max_lsn_;
  hdr.op = op;
  hdr.key_len = static_cast<uint16_t>(key.size());
  hdr.value_len = static_cast<uint16_t>(value.size());
  if (hdr.key_len != key.size() || hdr.value_len != value.size()) {
    return Status::InvalidArgument("wal record too large");
  }
  const Status st = AppendRecord(hdr, key, value);
  if (st.ok() && out_lsn) {
    *out_lsn = hdr.lsn;
  }
  return st;
}

Status WalWriter::AppendMany(std::vector<BatchItem>* items) {
  if (!items || items->empty()) return Status::Ok();
  std::lock_guard<std::mutex> lock(mu_);
  for (BatchItem& item : *items) {
    if (!item.key || !item.value) {
      item.status = Status::InvalidArgument("batch item missing key/value");
      continue;
    }
    WalRecordHeader hdr{};
    hdr.lsn = ++max_lsn_;
    hdr.op = item.op;
    hdr.key_len = static_cast<uint16_t>(item.key->size());
    hdr.value_len = static_cast<uint16_t>(item.value->size());
    if (hdr.key_len != item.key->size() || hdr.value_len != item.value->size()) {
      item.status = Status::InvalidArgument("wal record too large");
      continue;
    }
    item.status = AppendRecord(hdr, *item.key, *item.value);
    if (item.status.ok() && item.out_lsn) {
      *item.out_lsn = hdr.lsn;
    }
  }
  return Status::Ok();
}

Status WalWriter::FlushStagingLocked(bool force) {
#if defined(_WIN32)
  if (!write_through_ || staging_used_ == 0) {
    return Status::Ok();
  }
  if (!sync_handle_ || sync_handle_ == INVALID_HANDLE_VALUE ||
      !partial_handle_ || !aligned_io_buf_) {
    return Status::IoError("wal write-through handle failed");
  }

  auto write_at = [this](void* handle, const char* data, size_t len) -> Status {
    EnsureFileCapacityLocked(file_offset_ + len);
    HANDLE h = static_cast<HANDLE>(handle);
    LARGE_INTEGER pos{};
    pos.QuadPart = static_cast<LONGLONG>(file_offset_);
    if (!SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) {
      return Status::IoError("wal SetFilePointerEx failed");
    }
    DWORD written = 0;
    if (!WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr) ||
        written != len) {
      return Status::IoError("wal WriteFile failed");
    }
    file_offset_ += len;
    return Status::Ok();
  };

  while (staging_used_ >= kSectorSize) {
    std::memcpy(aligned_io_buf_, staging_buf_.data(), kSectorSize);
    const Status st = write_at(sync_handle_, aligned_io_buf_, kSectorSize);
    if (!st.ok()) return st;
    staging_used_ -= kSectorSize;
    if (staging_used_ > 0) {
      std::memmove(staging_buf_.data(), staging_buf_.data() + kSectorSize,
                   staging_used_);
    }
  }

  if (staging_used_ > 0 && force) {
    const Status st =
        write_at(partial_handle_, staging_buf_.data(), staging_used_);
    if (!st.ok()) return st;
    staging_used_ = 0;
  }

  file_.clear();
  file_.seekp(static_cast<std::streamoff>(file_offset_));
#endif
  return Status::Ok();
}

Status WalWriter::Fsync() {
  std::lock_guard<std::mutex> lock(mu_);
#if defined(_WIN32)
  if (write_through_) {
    const Status flush_st = FlushStagingLocked(true);
    if (!flush_st.ok()) return flush_st;
    for (const auto& e : pending_index_) {
      key_index_.Update(e.offset, e.key, e.lsn);
    }
    pending_index_.clear();
    return Status::Ok();
  }
#endif
  file_.flush();
  if (!file_) return Status::IoError("wal flush failed");
  for (const auto& e : pending_index_) {
    key_index_.Update(e.offset, e.key, e.lsn);
  }
  pending_index_.clear();
#if defined(_WIN32)
  if (!sync_handle_ || sync_handle_ == INVALID_HANDLE_VALUE) {
    return Status::IoError("wal fsync handle failed");
  }
  if (!FlushFileBuffers(static_cast<HANDLE>(sync_handle_))) {
    return Status::IoError("FlushFileBuffers failed");
  }
#else
  if (sync_fd_ < 0) return Status::IoError("wal fsync fd failed");
  if (::fdatasync(sync_fd_) != 0) return Status::IoError("fdatasync failed");
#endif
  return Status::Ok();
}

Status WalWriter::ReplayFrom(
    uint64_t after_lsn,
    const std::function<Status(WalOp, const std::string&, const std::string&)>&
        apply) {
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    return Status::IoError("wal open for replay failed");
  }
  while (in) {
    WalRecordHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in) break;
    if (hdr.lsn <= after_lsn) {
      std::string key(hdr.key_len, '\0');
      std::string value(hdr.value_len, '\0');
      if (hdr.key_len) in.read(key.data(), hdr.key_len);
      if (hdr.value_len) in.read(value.data(), hdr.value_len);
      continue;
    }
    std::string key(hdr.key_len, '\0');
    std::string value(hdr.value_len, '\0');
    if (hdr.key_len) in.read(key.data(), hdr.key_len);
    if (hdr.value_len) in.read(value.data(), hdr.value_len);
    const Status st = apply(hdr.op, key, value);
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

Status WalWriter::ReplayRecordAt(uint64_t offset, WalOp* op_out,
                                 std::string* key_out, std::string* value_out,
                                 uint64_t* lsn_out) {
  std::ifstream in(path_, std::ios::binary);
  if (!in) return Status::IoError("wal open for replay failed");
  in.seekg(static_cast<std::streamoff>(offset));
  WalRecordHeader hdr{};
  in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  if (!in) return Status::IoError("wal record read failed");
  std::string key(hdr.key_len, '\0');
  std::string value(hdr.value_len, '\0');
  if (hdr.key_len) in.read(key.data(), hdr.key_len);
  if (hdr.value_len) in.read(value.data(), hdr.value_len);
  if (op_out) *op_out = hdr.op;
  if (key_out) *key_out = std::move(key);
  if (value_out) *value_out = std::move(value);
  if (lsn_out) *lsn_out = hdr.lsn;
  return Status::Ok();
}

Status WalWriter::TruncateTo(uint64_t wal_lsn) {
  std::lock_guard<std::mutex> lock(mu_);
  staging_used_ = 0;
  file_.flush();
  file_.clear();
  file_.seekg(0, std::ios::end);
  const auto end = file_.tellg();
  uint64_t keep_from = end > 0 ? static_cast<uint64_t>(end) : 0;
  if (end > 0) {
    file_.seekg(0);
    while (file_.tellg() < end) {
      const auto offset = static_cast<uint64_t>(file_.tellg());
      WalRecordHeader hdr{};
      file_.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
      if (!file_) break;
      std::string key(hdr.key_len, '\0');
      std::string value(hdr.value_len, '\0');
      if (hdr.key_len) file_.read(key.data(), hdr.key_len);
      if (hdr.value_len) file_.read(value.data(), hdr.value_len);
      if (hdr.lsn > wal_lsn) {
        keep_from = offset;
        break;
      }
    }
  }
  std::vector<char> suffix;
  if (keep_from < static_cast<uint64_t>(end)) {
    const auto suffix_len = static_cast<size_t>(end) - keep_from;
    suffix.resize(suffix_len);
    file_.seekg(static_cast<std::streamoff>(keep_from));
    file_.read(suffix.data(), static_cast<std::streamsize>(suffix_len));
  }
  file_.close();
  std::filesystem::resize_file(path_, suffix.size());
  file_.open(path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
  if (!file_) return Status::IoError("wal reopen after truncate failed");
  if (!suffix.empty()) {
    file_.write(suffix.data(), static_cast<std::streamsize>(suffix.size()));
    if (!file_) return Status::IoError("wal rewrite after truncate failed");
  }
  file_offset_ = suffix.size();
  durable_capacity_ =
      ((file_offset_ + kSectorSize - 1) / kSectorSize) * kSectorSize;
#if defined(_WIN32)
  if (sync_handle_ && sync_handle_ != INVALID_HANDLE_VALUE) {
    HANDLE handle = static_cast<HANDLE>(sync_handle_);
    LARGE_INTEGER pos{};
    pos.QuadPart = static_cast<LONGLONG>(durable_capacity_);
    SetFilePointerEx(handle, pos, nullptr, FILE_BEGIN);
    SetEndOfFile(handle);
  }
#endif
  pending_index_.clear();
  const Status st = RebuildKeyIndex();
  if (!st.ok()) return st;
  if (max_lsn_ < wal_lsn) {
    max_lsn_ = wal_lsn;
  }
  return Status::Ok();
}

Status WalWriter::TruncateAfterAppendForTest() {
  std::lock_guard<std::mutex> lock(mu_);
  staging_used_ = 0;
  file_.flush();
  const auto end = file_.tellp();
  if (end <= static_cast<std::streamoff>(sizeof(WalRecordHeader))) {
    return RebuildKeyIndex();
  }
  const auto truncate_to =
      end - static_cast<std::streamoff>(sizeof(WalRecordHeader) / 2);
  file_.close();
  std::filesystem::resize_file(path_, static_cast<std::uintmax_t>(truncate_to));
  file_.open(path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
  if (!file_) return Status::IoError("wal reopen after truncate failed");
  return RebuildKeyIndex();
}

Status WalWriter::ReplayKey(uint64_t after_lsn, const std::string& key,
                            uint64_t* out_lsn) {
  std::lock_guard<std::mutex> lock(mu_);
  for (auto it = pending_index_.rbegin(); it != pending_index_.rend(); ++it) {
    if (it->key == key && it->lsn > after_lsn) {
      WalOp op = WalOp::kPut;
      std::string k;
      std::string value;
      uint64_t lsn = 0;
      const Status rs =
          ReplayRecordAt(it->offset, &op, &k, &value, &lsn);
      if (!rs.ok()) return rs;
      if (out_lsn) *out_lsn = lsn;
      return Status::Ok();
    }
  }
  uint64_t offset = 0;
  if (!key_index_.Lookup(key, after_lsn, &offset)) {
    return Status::NotFound("key not in wal index");
  }
  WalOp op = WalOp::kPut;
  std::string k;
  std::string value;
  uint64_t lsn = 0;
  const Status rs = ReplayRecordAt(offset, &op, &k, &value, &lsn);
  if (!rs.ok()) return rs;
  if (out_lsn) *out_lsn = lsn;
  return Status::Ok();
}

}  // namespace ebtree
