#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "ebtree/common/status.h"

namespace ebtree {

class WalKeyIndex {
 public:
  void Clear();
  void Update(uint64_t offset, const std::string& key, uint64_t lsn);
  bool Lookup(const std::string& key, uint64_t after_lsn,
              uint64_t* offset_out) const;
  bool LatestLsn(const std::string& key, uint64_t after_lsn,
                 uint64_t* lsn_out) const;
  Status BuildFromFile(const std::string& path);

  size_t size() const { return index_.size(); }

 private:
  struct Entry {
    uint64_t offset{0};
    uint64_t lsn{0};
  };
  std::unordered_map<std::string, Entry> index_;
};

}  // namespace ebtree
