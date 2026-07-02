#pragma once

#include "rar_types.h"

namespace ebtree {
namespace audit {

struct TierConsistencyReport {
  bool consistent{true};
  std::vector<TierConsistencyIssue> issues;
};

TierConsistencyReport CheckTierConsistency(const RarReport& report);

}  // namespace audit
}  // namespace ebtree
