#pragma once

#include <cstdint>
#include <unordered_map>

#include "ebtree/common/status.h"

namespace ebtree {

class DataFileLsnIndex {
 public:
  void Clear();
  void Update(uint64_t offset, uint64_t lsn);
  bool Lookup(uint64_t lsn, uint64_t* offset_out) const;
  Status BuildFromFile(const std::string& path);
  Status SaveToFile(const std::string& path) const;
  Status LoadFromFile(const std::string& path);

  size_t size() const { return index_.size(); }

 private:
  std::unordered_map<uint64_t, uint64_t> index_;
};

}  // namespace ebtree
