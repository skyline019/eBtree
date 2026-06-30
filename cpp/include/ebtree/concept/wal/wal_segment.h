#pragma once

#include "ebtree/common/status.h"
#include "ebtree/concept/wal/wal.h"

namespace ebtree {

class MemTable;

class WalSegmentReplayer {
 public:
  static bool HasPending(WalWriter* wal, uint64_t after_lsn);
  static Status ReplayPending(WalWriter* wal, MemTable* mt, uint64_t after_lsn);
};

}  // namespace ebtree
