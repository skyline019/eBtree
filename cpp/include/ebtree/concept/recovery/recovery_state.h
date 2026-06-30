#pragma once

#include "ebtree/common/config.h"

namespace ebtree {

enum class ShardRecoveryState {
  kWalCorrupt,
  kOnDiskLazy,
  kWalPending,
  kLazyKey,
  kCommittedCold,
  kCommittedHot,
};

struct RecoveryStateInput {
  bool wal_corrupt{false};
  bool wal_replay_pending{false};
  bool lazy_committed_load{false};
  bool committed_empty{true};
  bool memtables_empty{true};
  RecoveryMode recovery_mode{RecoveryMode::kHot};
  bool lazy_root_corrupt{false};
  bool btree_on_disk{false};
};

ShardRecoveryState ComputeRecoveryState(const RecoveryStateInput& in);

}  // namespace ebtree
