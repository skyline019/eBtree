#pragma once

#include <string>
#include <utility>
#include <vector>

#include "ebtree/common/status.h"
#include "ebtree/concept/btree/btree.h"

namespace ebtree {

class ShardEngine;

class ScanResolver {
 public:
  static Status Scan(ShardEngine& shard, const TypedPlan& plan,
                     std::vector<std::pair<std::string, std::string>>* rows);
};

}  // namespace ebtree
