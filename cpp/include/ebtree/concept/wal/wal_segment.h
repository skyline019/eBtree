#pragma once

#include <functional>
#include <string>

#include "ebtree/common/status.h"
#include "ebtree/concept/wal/wal.h"

namespace ebtree {

class MemTable;

class WalSegmentReplayer {
 public:
  static bool HasPending(WalWriter* wal, uint64_t after_lsn);
  static bool HasPendingOnDisk(const std::string& wal_path, uint64_t after_lsn);
  static uint64_t MaxLsnOnDisk(const std::string& wal_path, uint64_t after_lsn);
  static uint64_t DataOffsetOnDisk(const std::string& wal_path);
  static uint64_t LatestCommittedLsnForKey(const std::string& wal_path,
                                           const std::string& key,
                                           uint64_t after_lsn);
  static Status TailExtentOnDisk(const std::string& wal_path,
                                 uint64_t* data_end_out, uint64_t* max_lsn_out);
  using TailRecordFn = std::function<void(uint64_t offset,
                                          const WalRecordHeaderV2& hdr,
                                          const std::string& key)>;
  static Status ExtendTailFromOffset(const std::string& wal_path,
                                     uint64_t start_offset,
                                     uint64_t* data_end_out,
                                     uint64_t* max_lsn_out,
                                     TailRecordFn on_record = nullptr);
  static Status ReplayPending(WalWriter* wal, MemTable* mt, uint64_t after_lsn);
};

}  // namespace ebtree
