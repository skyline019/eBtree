#include "tier_consistency_attestor.h"

#include <vector>

namespace ebtree {
namespace audit {

namespace {

bool TierAllowedForState(const std::string& state, const std::string& tier) {
  if (state == "CommittedCold" || state == "CommittedHot") {
    return tier == "Committed" || tier == "CommittedDirectScan" ||
           tier == "BTreeScanResolve" || tier == "MemTable" ||
           tier == "BTreeDisk" || tier == "DataFileLsn";
  }
  if (state == "OnDiskLazy" || state == "LazyKey") {
    return tier == "BTreeScanResolve" || tier == "BTreeDisk" ||
           tier == "DataFileLsn" || tier == "WalSingleKey" || tier == "MemTable";
  }
  if (state == "WalPending" || state == "WalCorrupt") {
    return tier == "WalSingleKey" || tier == "TLogFlashback" ||
           tier == "BTreeScanResolve";
  }
  return true;
}

}  // namespace

TierConsistencyReport CheckTierConsistency(const RarReport& report) {
  TierConsistencyReport out{};
  for (const auto& probe : report.recovery.probes) {
    if (!probe.found || probe.read_tier.empty()) continue;
    const RecoveryShardReport* shard = nullptr;
    for (const auto& s : report.recovery.shard_state) {
      if (s.shard_id == probe.shard) {
        shard = &s;
        break;
      }
    }
    if (!shard) continue;
    if (TierAllowedForState(shard->state, probe.read_tier)) continue;
    TierConsistencyIssue issue{};
    issue.shard = probe.shard;
    issue.recovery_state = shard->state;
    issue.probe_key = probe.key;
    issue.expected_tier = shard->state;
    issue.actual_tier = probe.read_tier;
    out.issues.push_back(std::move(issue));
    out.consistent = false;
  }
  return out;
}

}  // namespace audit
}  // namespace ebtree
