#include "type_affinity.h"

#include <cctype>

namespace ebtree {
namespace sql {

namespace {

bool IsIntegerText(const std::string& s) {
  if (s.empty()) return false;
  size_t i = 0;
  if (s[0] == '-' || s[0] == '+') {
    if (s.size() == 1) return false;
    i = 1;
  }
  for (; i < s.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
  }
  return true;
}

bool IsRealText(const std::string& s) {
  if (s.empty()) return false;
  char* end = nullptr;
  std::strtod(s.c_str(), &end);
  return end != s.c_str() && *end == '\0';
}

double AsReal(const std::string& s) {
  return std::strtod(s.c_str(), nullptr);
}

int64_t AsInt64(const std::string& s) {
  return static_cast<int64_t>(std::strtoll(s.c_str(), nullptr, 10));
}

double SqlValueAsReal(const SqlValue& v) {
  switch (v.kind) {
    case SqlValueKind::kInteger:
      return static_cast<double>(v.i);
    case SqlValueKind::kReal:
      return v.r;
    case SqlValueKind::kText:
      return AsReal(v.text);
    default:
      return 0.0;
  }
}

TypeAffinity CombinedAffinity(TypeAffinity a, TypeAffinity b) {
  if (a == TypeAffinity::kNull) return b;
  if (b == TypeAffinity::kNull) return a;
  if (a == TypeAffinity::kReal || b == TypeAffinity::kReal) return TypeAffinity::kReal;
  if (a == TypeAffinity::kInteger && b == TypeAffinity::kInteger) {
    return TypeAffinity::kInteger;
  }
  return TypeAffinity::kText;
}

}  // namespace

TypeAffinity AffinityOf(const std::string& value) {
  if (value.empty()) return TypeAffinity::kNull;
  if (IsIntegerText(value)) return TypeAffinity::kInteger;
  if (IsRealText(value)) return TypeAffinity::kReal;
  return TypeAffinity::kText;
}

TypeAffinity AffinityOfValue(const SqlValue& value) {
  if (value.IsNull()) return TypeAffinity::kNull;
  switch (value.kind) {
    case SqlValueKind::kInteger:
      return TypeAffinity::kInteger;
    case SqlValueKind::kReal:
      return TypeAffinity::kReal;
    default:
      return AffinityOf(value.text);
  }
}

TypeAffinity AffinityFromColumnType(const std::string& col_type) {
  std::string u;
  u.reserve(col_type.size());
  for (char c : col_type) {
    u.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  if (u.find("INT") != std::string::npos) return TypeAffinity::kInteger;
  if (u.find("REAL") != std::string::npos || u.find("FLOA") != std::string::npos ||
      u.find("DOUB") != std::string::npos) {
    return TypeAffinity::kReal;
  }
  return TypeAffinity::kText;
}

bool CompareWithAffinity(const std::string& lhs, const std::string& rhs,
                         BinaryOp op) {
  return IsSqlTrue(CompareSqlValues(SqlValue::FromLegacyString(lhs),
                                    SqlValue::FromLegacyString(rhs), op,
                                    TypeAffinity::kText));
}

TruthValue CompareSqlValues(const SqlValue& lhs, const SqlValue& rhs,
                            BinaryOp op, TypeAffinity affinity_hint) {
  if (op == BinaryOp::kAnd || op == BinaryOp::kOr || op == BinaryOp::kAdd ||
      op == BinaryOp::kSub || op == BinaryOp::kMul || op == BinaryOp::kDiv) {
    return TruthValue::kFalse;
  }

  if (lhs.IsNull() || rhs.IsNull()) {
    if (op == BinaryOp::kEq) return TruthValue::kUnknown;
    if (op == BinaryOp::kNe) return TruthValue::kUnknown;
    return TruthValue::kUnknown;
  }

  if (op == BinaryOp::kEq || op == BinaryOp::kNe) {
    const TypeAffinity aff =
        CombinedAffinity(affinity_hint,
                         CombinedAffinity(AffinityOfValue(lhs), AffinityOfValue(rhs)));
    if (aff == TypeAffinity::kInteger) {
      const int64_t l = static_cast<int64_t>(SqlValueAsReal(lhs));
      const int64_t r = static_cast<int64_t>(SqlValueAsReal(rhs));
      const bool eq = l == r;
      return (op == BinaryOp::kEq) ? (eq ? TruthValue::kTrue : TruthValue::kFalse)
                                   : (eq ? TruthValue::kFalse : TruthValue::kTrue);
    }
    if (aff == TypeAffinity::kReal || aff == TypeAffinity::kInteger) {
      const double l = SqlValueAsReal(lhs);
      const double r = SqlValueAsReal(rhs);
      const bool eq = l == r;
      return (op == BinaryOp::kEq) ? (eq ? TruthValue::kTrue : TruthValue::kFalse)
                                   : (eq ? TruthValue::kFalse : TruthValue::kTrue);
    }
    const std::string l = lhs.ToLegacyString();
    const std::string r = rhs.ToLegacyString();
    const bool eq = l == r;
    return (op == BinaryOp::kEq) ? (eq ? TruthValue::kTrue : TruthValue::kFalse)
                                 : (eq ? TruthValue::kFalse : TruthValue::kTrue);
  }

  const TypeAffinity la = AffinityOfValue(lhs);
  const TypeAffinity ra = AffinityOfValue(rhs);
  bool numeric = (la == TypeAffinity::kInteger || la == TypeAffinity::kReal) &&
                 (ra == TypeAffinity::kInteger || ra == TypeAffinity::kReal);
  if (!numeric) {
    char* ea = nullptr;
    char* eb = nullptr;
    const double da = std::strtod(lhs.ToLegacyString().c_str(), &ea);
    const double db = std::strtod(rhs.ToLegacyString().c_str(), &eb);
    if (ea && *ea == '\0' && eb && *eb == '\0') numeric = true;
    if (numeric) {
      switch (op) {
        case BinaryOp::kLt:
          return da < db ? TruthValue::kTrue : TruthValue::kFalse;
        case BinaryOp::kLe:
          return da <= db ? TruthValue::kTrue : TruthValue::kFalse;
        case BinaryOp::kGt:
          return da > db ? TruthValue::kTrue : TruthValue::kFalse;
        case BinaryOp::kGe:
          return da >= db ? TruthValue::kTrue : TruthValue::kFalse;
        default:
          break;
      }
    }
  }
  if (numeric) {
    const double l = SqlValueAsReal(lhs);
    const double r = SqlValueAsReal(rhs);
    switch (op) {
      case BinaryOp::kLt:
        return l < r ? TruthValue::kTrue : TruthValue::kFalse;
      case BinaryOp::kLe:
        return l <= r ? TruthValue::kTrue : TruthValue::kFalse;
      case BinaryOp::kGt:
        return l > r ? TruthValue::kTrue : TruthValue::kFalse;
      case BinaryOp::kGe:
        return l >= r ? TruthValue::kTrue : TruthValue::kFalse;
      default:
        return TruthValue::kFalse;
    }
  }

  const std::string l = lhs.ToLegacyString();
  const std::string r = rhs.ToLegacyString();
  switch (op) {
    case BinaryOp::kLt:
      return l < r ? TruthValue::kTrue : TruthValue::kFalse;
    case BinaryOp::kLe:
      return l <= r ? TruthValue::kTrue : TruthValue::kFalse;
    case BinaryOp::kGt:
      return l > r ? TruthValue::kTrue : TruthValue::kFalse;
    case BinaryOp::kGe:
      return l >= r ? TruthValue::kTrue : TruthValue::kFalse;
    default:
      return TruthValue::kFalse;
  }
}

}  // namespace sql
}  // namespace ebtree
