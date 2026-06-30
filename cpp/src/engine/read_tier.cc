#include "ebtree/engine/read_tier.h"

namespace ebtree {

void RecordReadTier(ReadTier tier, uint64_t* tier_hits, uint64_t* unexpected_path) {
  if (!tier_hits) return;
  const size_t idx = static_cast<size_t>(tier);
  if (idx < kReadTierCount) {
    ++tier_hits[idx];
  } else if (unexpected_path) {
    ++(*unexpected_path);
  }
}

}  // namespace ebtree
