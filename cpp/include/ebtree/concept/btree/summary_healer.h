#pragma once

#include <unordered_map>
#include <utility>
#include <string>

#include "ebtree/common/status.h"
#include "ebtree/concept/btree/btree.h"

namespace ebtree {

class SummaryHealer {
 public:
  static Status RebuildFromIndex(BTreeIndex* btree);
  static Status RebuildFromCommitted(
      BTreeIndex* btree,
      const std::unordered_map<std::string, std::pair<std::string, uint64_t>>&
          committed);
};

}  // namespace ebtree
