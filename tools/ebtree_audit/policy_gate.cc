#include "policy_gate.h"

#include "tier_consistency_attestor.h"

namespace ebtree {
namespace audit {

RarVerdict EvaluateSnapshotPolicy(const AttestExportReportV2& snapshot,
                                  const RarPolicy& policy,
                                  std::string* reason_out) {
  if (reason_out) reason_out->clear();

  if (policy.require_unexpected_path_zero &&
      snapshot.base.recovery.unexpected_path_total > 0) {
    if (reason_out) *reason_out = "unexpected_path_total > 0";
    return RarVerdict::kRefuseStart;
  }

  if (snapshot.compress.decompress_fail > policy.max_decompress_fail) {
    if (reason_out) *reason_out = "decompress_fail exceeds max_decompress_fail";
    return RarVerdict::kRefuseStart;
  }

  return RarVerdict::kPass;
}

RarVerdict ApplyPolicyGate(const RarReport& report, std::string* reason_out) {
  if (reason_out) reason_out->clear();

  if (report.policy.require_unexpected_path_zero &&
      report.recovery.unexpected_path_total > 0) {
    if (reason_out) {
      *reason_out = "unexpected_path_total > 0";
    }
    return RarVerdict::kRefuseStart;
  }

  if (report.policy.require_tier_consistent) {
    const TierConsistencyReport tier = CheckTierConsistency(report);
    if (!tier.consistent) {
      if (reason_out) *reason_out = "tier consistency mismatch";
      return RarVerdict::kRefuseStart;
    }
  }

  const uint64_t decompress_fail = report.kernel.decompress_fail;
  if (decompress_fail > report.policy.max_decompress_fail) {
    if (reason_out) {
      *reason_out = "decompress_fail exceeds max_decompress_fail";
    }
    return RarVerdict::kRefuseStart;
  }

  const size_t missing_count = report.contract.missing.size();
  if (missing_count > report.policy.recovery_max_missing) {
    if (reason_out) {
      *reason_out = "missing keys exceed recovery_max_missing";
    }
    return RarVerdict::kRefuseStart;
  }

  if (!report.policy.allow_unexpected_keys &&
      !report.contract.unexpected.empty()) {
    if (reason_out) {
      *reason_out = "unexpected keys present";
    }
    return RarVerdict::kRefuseStart;
  }

  if (report.contract.mode == ContractMode::kDurable &&
      !report.contract.pending_uncommitted.empty()) {
    if (reason_out) {
      *reason_out = "pending uncommitted keys visible after recovery";
    }
    return RarVerdict::kWarn;
  }

  return RarVerdict::kPass;
}

}  // namespace audit
}  // namespace ebtree
