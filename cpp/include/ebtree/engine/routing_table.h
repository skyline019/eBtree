#pragma once

#include <cstdint>

namespace ebtree {

struct alignas(64) RoutingTable {
  uint32_t shard_count{0};
  uint32_t slots[256]{};
};

RoutingTable BuildRoutingTable(uint32_t shard_count);

}  // namespace ebtree
