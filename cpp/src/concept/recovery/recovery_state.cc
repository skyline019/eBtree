#include "ebtree/concept/recovery/recovery_state.h"

namespace ebtree {

ShardRecoveryState ComputeRecoveryState(const RecoveryStateInput& in) {
  if (in.wal_corrupt) return ShardRecoveryState::kWalCorrupt;
  if (in.wal_replay_pending) return ShardRecoveryState::kWalPending;
  if (in.recovery_mode == RecoveryMode::kLazy || in.lazy_root_corrupt) {
    return ShardRecoveryState::kLazyKey;
  }
  if (!in.committed_empty && in.memtables_empty) {
    return ShardRecoveryState::kCommittedCold;
  }
  if (in.lazy_committed_load || (in.committed_empty && in.btree_on_disk)) {
    return ShardRecoveryState::kOnDiskLazy;
  }
  return ShardRecoveryState::kCommittedHot;
}

}  // namespace ebtree
