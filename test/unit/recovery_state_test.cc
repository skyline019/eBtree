#include <gtest/gtest.h>

#include "ebtree/concept/recovery/recovery_state.h"

namespace ebtree {
namespace {

TEST(RecoveryStateTest, CommittedColdWhenCommittedAndEmptyMemtables) {
  RecoveryStateInput in;
  in.committed_empty = false;
  in.memtables_empty = true;
  EXPECT_EQ(ComputeRecoveryState(in), ShardRecoveryState::kCommittedCold);
}

TEST(RecoveryStateTest, WalCorruptTakesPrecedence) {
  RecoveryStateInput in;
  in.wal_corrupt = true;
  in.committed_empty = false;
  EXPECT_EQ(ComputeRecoveryState(in), ShardRecoveryState::kWalCorrupt);
}

TEST(RecoveryStateTest, OnDiskLazyWhenOptIn) {
  RecoveryStateInput in;
  in.lazy_committed_load = true;
  EXPECT_EQ(ComputeRecoveryState(in), ShardRecoveryState::kOnDiskLazy);
}

TEST(RecoveryStateTest, WalPendingWhenReplayNeeded) {
  RecoveryStateInput in;
  in.wal_replay_pending = true;
  EXPECT_EQ(ComputeRecoveryState(in), ShardRecoveryState::kWalPending);
}

TEST(RecoveryStateTest, LazyKeyWhenRecoveryModeLazy) {
  RecoveryStateInput in;
  in.recovery_mode = RecoveryMode::kLazy;
  EXPECT_EQ(ComputeRecoveryState(in), ShardRecoveryState::kLazyKey);
}

TEST(RecoveryStateTest, CommittedHotDefault) {
  RecoveryStateInput in;
  in.committed_empty = false;
  in.memtables_empty = false;
  EXPECT_EQ(ComputeRecoveryState(in), ShardRecoveryState::kCommittedHot);
}

}  // namespace
}  // namespace ebtree
