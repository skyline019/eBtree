#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/status.h"

namespace ebtree {

struct TxnSidecarEntry {
  uint32_t txn_id{0};
  uint64_t snapshot_lsn{0};
  bool committed{false};
  uint64_t commit_lsn{0};
};

class TxnSidecarStore {
 public:
  static std::string PathForShard(const std::string& engine_path, uint32_t shard_id);

  void SetOpenTxn(uint32_t txn_id, uint64_t snapshot_lsn);
  void MarkCommitted(uint32_t txn_id, uint64_t commit_lsn);
  void MarkAborted(uint32_t txn_id);
  const std::vector<TxnSidecarEntry>& entries() const { return entries_; }
  uint64_t last_commit_lsn() const { return last_commit_lsn_; }

  Status SaveToFile(const std::string& path) const;
  Status LoadFromFile(const std::string& path);

 private:
  TxnSidecarEntry* Find(uint32_t txn_id);

  std::vector<TxnSidecarEntry> entries_;
  uint64_t last_commit_lsn_{0};
};

}  // namespace ebtree
