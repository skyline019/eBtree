#include "ebtree/engine/routing_table.h"

#include "ebtree/engine/shard_router.h"

namespace ebtree {

RoutingTable BuildRoutingTable(uint32_t shard_count) {
  RoutingTable table{};
  table.shard_count = shard_count;
  if (shard_count == 0) return table;
  for (uint32_t i = 0; i < 256; ++i) {
    const std::string key(1, static_cast<char>(i));
    table.slots[i] = RouteShard(key, shard_count);
  }
  return table;
}

}  // namespace ebtree
