#pragma once

namespace ebtree {
namespace sql {

enum class TruthValue { kTrue, kFalse, kUnknown };

inline TruthValue And3(TruthValue a, TruthValue b) {
  if (a == TruthValue::kFalse || b == TruthValue::kFalse) return TruthValue::kFalse;
  if (a == TruthValue::kUnknown || b == TruthValue::kUnknown) return TruthValue::kUnknown;
  return TruthValue::kTrue;
}

inline TruthValue Or3(TruthValue a, TruthValue b) {
  if (a == TruthValue::kTrue || b == TruthValue::kTrue) return TruthValue::kTrue;
  if (a == TruthValue::kUnknown || b == TruthValue::kUnknown) return TruthValue::kUnknown;
  return TruthValue::kFalse;
}

inline TruthValue Not3(TruthValue tv) {
  if (tv == TruthValue::kTrue) return TruthValue::kFalse;
  if (tv == TruthValue::kFalse) return TruthValue::kTrue;
  return TruthValue::kUnknown;
}

inline bool IsSqlTrue(TruthValue tv) { return tv == TruthValue::kTrue; }

}  // namespace sql
}  // namespace ebtree
