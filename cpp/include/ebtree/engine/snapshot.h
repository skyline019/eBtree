#pragma once

#include <cstdint>
#include <vector>

namespace ebtree {

struct SnapshotToken {
  std::vector<uint64_t> shard_lsns;

  uint64_t ForShard(uint32_t shard_id) const {
    if (shard_id < shard_lsns.size()) return shard_lsns[shard_id];
    return shard_lsns.empty() ? 0 : shard_lsns[0];
  }
};

struct SnapshotReadContext {
  uint64_t snapshot_lsn{0};
  uint32_t reader_txn_id{0};
};

}  // namespace ebtree
