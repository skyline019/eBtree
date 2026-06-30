#include "rar_types.h"

#include "ebtree/concept/recovery/recovery_state.h"
#include "ebtree/engine/engine_attest.h"

namespace ebtree {
namespace audit {

std::string DurabilityClassToString(DurabilityClass tier) {
  switch (tier) {
    case DurabilityClass::kSync:
      return "sync";
    case DurabilityClass::kBalanced:
      return "balanced";
    case DurabilityClass::kGroup:
      return "group";
    case DurabilityClass::kAsync:
      return "async";
  }
  return "balanced";
}

DurabilityClass DurabilityClassFromString(const std::string& s) {
  if (s == "sync") return DurabilityClass::kSync;
  if (s == "group") return DurabilityClass::kGroup;
  if (s == "async") return DurabilityClass::kAsync;
  return DurabilityClass::kBalanced;
}

std::string ContractModeToString(ContractMode mode) {
  return mode == ContractMode::kDurable ? "durable" : "visibility";
}

std::string InferredRecoveryPathToString(InferredRecoveryPath path) {
  switch (path) {
    case InferredRecoveryPath::kFastOpenDeferred:
      return "FastOpenDeferred";
    case InferredRecoveryPath::kWalReplayComplete:
      return "WalReplayComplete";
    case InferredRecoveryPath::kTLogFallback:
      return "TLogFallback";
    case InferredRecoveryPath::kLazyKey:
      return "LazyKey";
    case InferredRecoveryPath::kOnDiskLazy:
      return "OnDiskLazy";
    case InferredRecoveryPath::kCommittedHot:
      return "CommittedHot";
    case InferredRecoveryPath::kUnknown:
      break;
  }
  return "Unknown";
}

std::string RarVerdictToString(RarVerdict verdict) {
  switch (verdict) {
    case RarVerdict::kPass:
      return "PASS";
    case RarVerdict::kWarn:
      return "WARN";
    case RarVerdict::kRefuseStart:
      return "REFUSE_START";
  }
  return "PASS";
}

}  // namespace audit
}  // namespace ebtree
