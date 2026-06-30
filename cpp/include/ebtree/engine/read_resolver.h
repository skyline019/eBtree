#pragma once

#include <string>

#include "ebtree/common/status.h"

namespace ebtree {

class ShardEngine;
struct TypedPlan;

class ReadResolver {
 public:
  static Status Get(ShardEngine& shard, const std::string& key, std::string* value);

 private:
  static bool TryLazyWalOrDisk(ShardEngine& shard, const std::string& key,
                               const TypedPlan& plan, std::string* value);
};

}  // namespace ebtree
