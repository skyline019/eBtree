#include "ebtree/engine/shard_router.h"

namespace ebtree {

namespace {

uint32_t Fnv1a32(const std::string& key) {
  uint32_t hash = 2166136261u;
  for (unsigned char c : key) {
    hash ^= c;
    hash *= 16777619u;
  }
  return hash;
}

}  // namespace

uint32_t RouteShard(const std::string& key, uint32_t shard_count) {
  if (shard_count <= 1) return 0;
  return Fnv1a32(key) % shard_count;
}

Status ValidateShardCount(uint32_t shard_count) {
  if (shard_count == 1 || shard_count == 4 || shard_count == 16 ||
      shard_count == 256) {
    return Status::Ok();
  }
  return Status::InvalidArgument("shard_count must be 1, 4, 16, or 256");
}

}  // namespace ebtree
