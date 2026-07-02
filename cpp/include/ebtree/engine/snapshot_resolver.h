#pragma once

#include <string>

#include "ebtree/concept/memtable/memtable.h"
#include "ebtree/common/status.h"
#include "ebtree/engine/snapshot.h"

namespace ebtree {

class ShardEngine;

bool MemEntryVisible(const MemTableEntry& entry, const SnapshotReadContext& ctx);

class SnapshotResolver {
 public:
  static Status ResolveAtSnapshot(ShardEngine& shard, const std::string& key,
                                  const SnapshotReadContext& ctx,
                                  std::string* value);
  static Status ResolveLsnAtSnapshot(ShardEngine& shard, const std::string& key,
                                     uint64_t snapshot_lsn, uint64_t* lsn_out);
  static Status ResolveCurrentCommittedLsn(ShardEngine& shard,
                                           const std::string& key,
                                           uint64_t* lsn_out);
};

}  // namespace ebtree
