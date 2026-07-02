#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/status.h"

namespace ebtree {

struct MemTableEntry {
  std::string value;
  uint64_t lsn{0};
  bool deleted{false};
  uint32_t txn_id{0};
  bool durable{true};
};

class MemTable {
 public:
  Status Put(const std::string& key, const std::string& value, uint64_t lsn,
             uint32_t txn_id = 0, bool durable = true);
  Status DeleteKey(const std::string& key, uint64_t lsn, uint32_t txn_id = 0,
                   bool durable = true);
  std::optional<MemTableEntry> Get(const std::string& key) const;
  std::vector<std::pair<std::string, MemTableEntry>> Snapshot() const;
  bool Empty() const;
  void Clear();
  void Swap(MemTable* other);
  void PromoteTxn(uint32_t txn_id);
  void ClearTxn(uint32_t txn_id);

 private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, MemTableEntry> map_;
};

}  // namespace ebtree
