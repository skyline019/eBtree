#include "policy_gate.h"

namespace ebtree {
namespace audit {

RarVerdict ApplyPolicyGate(const RarReport& report, std::string* reason_out) {
  if (reason_out) reason_out->clear();

  if (report.policy.require_unexpected_path_zero &&
      report.recovery.unexpected_path_total > 0) {
    if (reason_out) {
      *reason_out = "unexpected_path_total > 0";
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
