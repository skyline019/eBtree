#pragma once

#include "ebtree/common/config.h"

namespace ebtree {

struct LazyRecoveryState {
  uint64_t wal_replay_cursor{0};
  uint64_t pending_wal_bytes{0};
  uint64_t lazy_page_faults{0};
};

}  // namespace ebtree
