#pragma once

#include <cstdint>
#include <string>

#include "ebtree/common/status.h"

namespace ebtree {

uint32_t RouteShard(const std::string& key, uint32_t shard_count);

Status ValidateShardCount(uint32_t shard_count);

}  // namespace ebtree
