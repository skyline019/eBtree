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
};

class MemTable {
 public:
  Status Put(const std::string& key, const std::string& value, uint64_t lsn);
  Status DeleteKey(const std::string& key, uint64_t lsn);
  std::optional<MemTableEntry> Get(const std::string& key) const;
  std::vector<std::pair<std::string, MemTableEntry>> Snapshot() const;
  bool Empty() const;
  void Clear();
  void Swap(MemTable* other);

 private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, MemTableEntry> map_;
};

}  // namespace ebtree
