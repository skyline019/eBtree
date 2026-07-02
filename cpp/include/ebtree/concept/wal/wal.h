#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "ebtree/common/status.h"
#include "ebtree/concept/wal/wal_key_index.h"

namespace ebtree {

enum class WalOp : uint8_t {
  kPut = 1,
  kDelete = 2,
  kTxnBegin = 3,
  kTxnCommit = 4,
  kTxnAbort = 5,
};

constexpr uint64_t kWalMagicV2 = 0xEB57414C32303031ULL;
constexpr size_t kWalSectorSize = 4096;

#pragma pack(push, 1)
struct WalRecordHeader {
  uint64_t lsn{0};
  WalOp op{WalOp::kPut};
  uint16_t key_len{0};
  uint16_t value_len{0};
  uint32_t record_crc{0};
};

struct WalRecordHeaderV2 {
  uint64_t lsn{0};
  WalOp op{WalOp::kPut};
  uint16_t key_len{0};
  uint16_t value_len{0};
  uint32_t txn_id{0};
  uint32_t record_crc{0};
};
#pragma pack(pop)

class WalWriter {
 public:
  explicit WalWriter(std::string path);
  ~WalWriter();

  Status Append(WalOp op, const std::string& key, const std::string& value,
                uint64_t* out_lsn, uint32_t txn_id = 0);
  Status AppendTxnControl(WalOp op, uint32_t txn_id, uint64_t meta_lsn,
                         uint64_t* out_lsn);
  struct BatchItem {
    WalOp op{WalOp::kPut};
    const std::string* key{nullptr};
    const std::string* value{nullptr};
    uint64_t* out_lsn{nullptr};
    uint32_t txn_id{0};
    Status status{Status::Ok()};
  };
  Status AppendMany(std::vector<BatchItem>* items);
  Status Fsync();
  Status ReplayFrom(uint64_t after_lsn,
                    const std::function<Status(WalOp, const std::string&,
                                               const std::string&)>& apply);
  Status ReplayKey(uint64_t after_lsn, const std::string& key,
                   uint64_t* out_lsn);
  Status ReplayRecordAt(uint64_t offset, WalOp* op_out, std::string* key_out,
                        std::string* value_out, uint64_t* lsn_out);

  uint64_t max_lsn() const { return max_lsn_; }
  const std::string& path() const { return path_; }
  const WalKeyIndex& key_index() const { return key_index_; }

  Status TruncateAfterAppendForTest();
  Status TruncateTo(uint64_t wal_lsn);

  void SetWriteThrough(bool enable);

  size_t UnflushedBytes() const;

  bool format_v2() const { return format_v2_; }

  void EnsureMinLsn(uint64_t min_lsn);

  // Incrementally ingest WAL bytes written by other engine instances.
  Status SyncExternalTail();
  bool LatestLsnForKey(const std::string& key, uint64_t after_lsn,
                       uint64_t* lsn_out) const;

 private:
  struct PendingIndexEntry {
    uint64_t offset;
    std::string key;
    uint64_t lsn;
  };

  Status AppendRecord(const WalRecordHeader& hdr, const std::string& key,
                      const std::string& value);
  Status RebuildKeyIndex();
  Status FlushStagingLocked(bool force);
  Status SyncExternalTailLocked();
  void EnsureStagingCapacity(size_t extra);
  void EnsureFileCapacityLocked(uint64_t end_offset);

  Status AppendRecordV2(const WalRecordHeaderV2& hdr, const std::string& key,
                        const std::string& value);
  Status EnsureWalMagicLocked();

  std::string path_;
  std::fstream file_;
  mutable std::mutex mu_;
  uint64_t max_lsn_{0};
  bool format_v2_{false};
  WalKeyIndex key_index_;
  std::vector<PendingIndexEntry> pending_index_;
  bool write_through_{false};
  bool no_buffering_{false};
  uint64_t file_offset_{0};
  uint64_t tail_data_end_{0};
  uint64_t durable_capacity_{0};
  std::vector<char> staging_buf_;
  size_t staging_used_{0};
#if defined(_WIN32)
  void* sync_handle_{nullptr};
  void* partial_handle_{nullptr};
  char* aligned_io_buf_{nullptr};
  size_t aligned_io_cap_{0};
#else
  int sync_fd_{-1};
#endif
};

}  // namespace ebtree
