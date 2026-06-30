#pragma once

#include "ebtree/common/config.h"
#include "ebtree/common/status.h"
#include "ebtree/concept/wal/wal.h"

namespace ebtree {

struct GroupCommitState {
  uint64_t pending_stable_lsn{0};
  uint32_t puts_since_commit{0};
};

class GroupCommitter {
 public:
  static Status Commit(WalWriter* wal, EngineStats* stats,
                       GroupCommitState* state);
  static void RecordPut(uint64_t lsn, GroupCommitState* state);
  static bool ShouldAutoCommit(const GroupCommitState& state,
                               uint32_t batch_size);
};

}  // namespace ebtree
