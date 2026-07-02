#pragma once

#include "ebtree/concept/btree/btree.h"
#include "ebtree/concept/recovery/recovery_state.h"

namespace ebtree {

enum class ReadOpKind : uint8_t { kGet, kScan };

enum class TierDispatchRoute : uint8_t {
  kDirectCommittedScan,
  kBTreeScanResolve,
  kStandardGet,
};

struct TierDispatchKey {
  ShardRecoveryState state{ShardRecoveryState::kCommittedHot};
  ReadOpKind op{ReadOpKind::kGet};

  friend bool operator==(const TierDispatchKey& a, const TierDispatchKey& b) {
    return a.state == b.state && a.op == b.op;
  }
};

inline TierDispatchRoute LookupTierDispatch(ShardRecoveryState state,
                                            ReadOpKind op, bool committed_direct) {
  if (op == ReadOpKind::kScan && committed_direct) {
    return TierDispatchRoute::kDirectCommittedScan;
  }
  if (op == ReadOpKind::kScan &&
      (state == ShardRecoveryState::kOnDiskLazy ||
       state == ShardRecoveryState::kCommittedHot ||
       state == ShardRecoveryState::kLazyKey)) {
    return TierDispatchRoute::kBTreeScanResolve;
  }
  return TierDispatchRoute::kStandardGet;
}

}  // namespace ebtree
