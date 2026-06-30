#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ebtree {
namespace audit {

inline std::string ShardPathPrefix(const std::string& base, uint32_t shard_id) {
  return base + "/shard" + std::to_string(shard_id);
}

inline std::string ShardSuperPath(const std::string& base, uint32_t shard_id) {
  return ShardPathPrefix(base, shard_id) + ".super";
}

inline std::string ShardWalPath(const std::string& base, uint32_t shard_id) {
  return ShardPathPrefix(base, shard_id) + ".wal";
}

inline std::string ShardDataPath(const std::string& base, uint32_t shard_id) {
  return ShardPathPrefix(base, shard_id) + ".data";
}

inline std::string ShardTLogPath(const std::string& base, uint32_t shard_id) {
  return ShardPathPrefix(base, shard_id) + ".tlog";
}

inline uint32_t DiscoverShardCount(const std::string& engine_path) {
  uint32_t count = 0;
  while (std::filesystem::exists(ShardSuperPath(engine_path, count)) ||
         std::filesystem::exists(ShardWalPath(engine_path, count)) ||
         std::filesystem::exists(ShardDataPath(engine_path, count))) {
    ++count;
  }
  return count > 0 ? count : 1;
}

}  // namespace audit
}  // namespace ebtree
