#include "expr_eval.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "type_affinity.h"

namespace ebtree {
namespace sql {

namespace {

int64_t AsInt(const std::string& s) {
  try {
    return std::stoll(s);
  } catch (...) {
    return 0;
  }
}

double AsDouble(const SqlValue& v) {
  switch (v.kind) {
    case SqlValueKind::kInteger:
      return static_cast<double>(v.i);
    case SqlValueKind::kReal:
      return v.r;
    case SqlValueKind::kText: {
      char* end = nullptr;
      const double d = std::strtod(v.text.c_str(), &end);
      if (end != v.text.c_str() && *end == '\0') return d;
      return 0.0;
    }
    default:
      return 0.0;
  }
}

}  // namespace

TypeAffinity ExprEval::ColumnAffinityFor(const ExprNode& node) const {
  if (node.kind == ExprKind::kColumn) {
    const auto dot = node.column.find('.');
    const std::string bare =
        dot == std::string::npos ? node.column : node.column.substr(dot + 1);
    return schema_.ColumnAffinity(bare);
  }
  return TypeAffinity::kText;
}

std::string ExprEval::LookupColumn(const ExprNode& node,
                                   const RowMap& row) const {
  if (!node.table.empty()) {
    const std::string qual = node.table + "." + node.column;
    const auto it = row.find(qual);
    if (it != row.end()) return it->second;
  }
  const auto it = row.find(node.column);
  if (it != row.end()) return it->second;
  const auto dot = node.column.find('.');
  if (dot != std::string::npos) {
    const auto it2 = row.find(node.column.substr(dot + 1));
    if (it2 != row.end()) return it2->second;
  }
  return "";
}

TruthValue ExprEval::EvalInLiterals(
    const SqlValue& lhs, const std::vector<std::string>& lits,
    bool not_op) const {
  if (lits.empty()) {
    return not_op ? TruthValue::kTrue : TruthValue::kFalse;
  }
  if (lhs.IsNull()) return TruthValue::kUnknown;
  bool found = false;
  bool seen_null = false;
  for (const auto& lit : lits) {
    if (lit.empty()) {
      seen_null = true;
      continue;
    }
    if (IsSqlTrue(CompareSqlValues(lhs, SqlValue::FromLegacyString(lit),
                                   BinaryOp::kEq, TypeAffinity::kText))) {
      found = true;
    }
  }
  if (found) return not_op ? TruthValue::kFalse : TruthValue::kTrue;
  if (seen_null) return TruthValue::kUnknown;
  return not_op ? TruthValue::kTrue : TruthValue::kFalse;
}

TruthValue ExprEval::EvalSubqueryTruth(const ExprNode& node,
                                       const RowMap& row) const {
  if (!node.subquery.has_value()) return TruthValue::kFalse;
  const SubquerySpec& sq = *node.subquery;
  if (sq.exists) {
    const TruthValue tv =
        subquery_fn_ ? subquery_fn_(node, row) : TruthValue::kFalse;
    return sq.not_op ? Not3(tv) : tv;
  }
  if (sq.lhs) {
    const SqlValue lhs = EvalValue(*sq.lhs, row);
    if (!sq.in_literals.empty()) {
      return EvalInLiterals(lhs, sq.in_literals, sq.not_op);
    }
    if (!sq.parsed_query) {
      return sq.not_op ? TruthValue::kTrue : TruthValue::kFalse;
    }
    const TruthValue tv =
        subquery_fn_ ? subquery_fn_(node, row) : TruthValue::kFalse;
    return tv;
  }
  return TruthValue::kFalse;
}

SqlValue ExprEval::EvalBinaryValue(const ExprNode& node,
                                   const RowMap& row) const {
  const SqlValue l = EvalValue(*node.children[0], row);
  const SqlValue r = EvalValue(*node.children[1], row);
  if (node.bin_op == BinaryOp::kAdd || node.bin_op == BinaryOp::kSub ||
      node.bin_op == BinaryOp::kMul || node.bin_op == BinaryOp::kDiv) {
    if (l.IsNull() || r.IsNull()) return SqlValue::Null();
    const double lv = AsDouble(l);
    const double rv = AsDouble(r);
    switch (node.bin_op) {
      case BinaryOp::kAdd:
        return SqlValue::Real(lv + rv);
      case BinaryOp::kSub:
        return SqlValue::Real(lv - rv);
      case BinaryOp::kMul:
        return SqlValue::Real(lv * rv);
      case BinaryOp::kDiv:
        if (rv == 0.0) return SqlValue::Null();
        return SqlValue::Real(lv / rv);
      default:
        break;
    }
  }
  return SqlValue::Null();
}

SqlValue ExprEval::EvalValue(const ExprNode& node, const RowMap& row) const {
  switch (node.kind) {
    case ExprKind::kLiteral:
      return SqlValue::FromLegacyString(node.literal);
    case ExprKind::kColumn:
      return SqlValue::FromLegacyString(LookupColumn(node, row));
    case ExprKind::kFunction:
      if (node.func_name == "LENGTH" && !node.children.empty()) {
        return SqlValue::Integer(
            static_cast<int64_t>(EvalScalar(*node.children[0], row).size()));
      }
      if (node.func_name == "SUBSTR" && node.children.size() >= 2) {
        const std::string s = EvalScalar(*node.children[0], row);
        const int64_t start = AsInt(EvalScalar(*node.children[1], row));
        const size_t pos = start < 0 ? 0 : static_cast<size_t>(start);
        if (pos >= s.size()) return SqlValue::Null();
        return SqlValue::Text(s.substr(pos));
      }
      if (node.func_name == "TYPEOF" && !node.children.empty()) {
        const SqlValue v = EvalValue(*node.children[0], row);
        if (v.IsNull()) return SqlValue::Text("null");
        if (v.kind == SqlValueKind::kInteger) return SqlValue::Text("integer");
        if (v.kind == SqlValueKind::kReal) return SqlValue::Text("real");
        return SqlValue::Text("text");
      }
      if (node.func_name == "ABS" && !node.children.empty()) {
        const SqlValue v = EvalValue(*node.children[0], row);
        if (v.IsNull()) return SqlValue::Null();
        return SqlValue::Integer(std::llabs(AsInt(v.ToLegacyString())));
      }
      if (node.func_name == "COALESCE") {
        for (const auto& c : node.children) {
          const SqlValue v = EvalValue(*c, row);
          if (!v.IsNull()) return v;
        }
        return SqlValue::Null();
      }
      return SqlValue::Null();
    case ExprKind::kBinary:
      if (node.bin_op == BinaryOp::kAdd || node.bin_op == BinaryOp::kSub ||
          node.bin_op == BinaryOp::kMul || node.bin_op == BinaryOp::kDiv) {
        return EvalBinaryValue(node, row);
      }
      return IsSqlTrue(EvalTruth(node, row)) ? SqlValue::Integer(1)
                                             : SqlValue::Integer(0);
    case ExprKind::kIsNull: {
      const SqlValue v = EvalValue(*node.children[0], row);
      const bool is_null = v.IsNull();
      const bool result = node.is_null_check ? is_null : !is_null;
      return SqlValue::Integer(result ? 1 : 0);
    }
    case ExprKind::kSubquery:
      return IsSqlTrue(EvalSubqueryTruth(node, row)) ? SqlValue::Integer(1)
                                                     : SqlValue::Integer(0);
    default:
      return SqlValue::Null();
  }
}

TruthValue ExprEval::EvalTruth(const ExprNode& node, const RowMap& row) const {
  TruthValue tv = TruthValue::kFalse;
  if (node.is_not && node.children.empty()) {
    // Handled per-kind below via temporary flip at end.
  }
  switch (node.kind) {
    case ExprKind::kLiteral: {
      const std::string& lit = node.literal;
      if (lit.empty() || lit == "NULL") {
        tv = TruthValue::kUnknown;
        break;
      }
      if (lit == "0" || lit == "false" || lit == "FALSE") {
        tv = TruthValue::kFalse;
        break;
      }
      tv = TruthValue::kTrue;
      break;
    }
    case ExprKind::kIsNull: {
      const SqlValue v = EvalValue(*node.children[0], row);
      const bool is_null = v.IsNull();
      if (node.is_null_check) {
        tv = is_null ? TruthValue::kTrue : TruthValue::kFalse;
      } else {
        tv = is_null ? TruthValue::kFalse : TruthValue::kTrue;
      }
      break;
    }
    case ExprKind::kBinary: {
      if (node.bin_op == BinaryOp::kAnd) {
        tv = And3(EvalTruth(*node.children[0], row),
                  EvalTruth(*node.children[1], row));
        break;
      }
      if (node.bin_op == BinaryOp::kOr) {
        tv = Or3(EvalTruth(*node.children[0], row),
                 EvalTruth(*node.children[1], row));
        break;
      }
      if (node.bin_op == BinaryOp::kAdd || node.bin_op == BinaryOp::kSub ||
          node.bin_op == BinaryOp::kMul || node.bin_op == BinaryOp::kDiv) {
        const SqlValue v = EvalBinaryValue(node, row);
        tv = v.IsNull() ? TruthValue::kUnknown
                        : (AsDouble(v) != 0.0 ? TruthValue::kTrue
                                              : TruthValue::kFalse);
        break;
      }
      const SqlValue l = EvalValue(*node.children[0], row);
      const SqlValue r = EvalValue(*node.children[1], row);
      TypeAffinity aff = ColumnAffinityFor(*node.children[0]);
      if (aff == TypeAffinity::kText) {
        aff = ColumnAffinityFor(*node.children[1]);
      }
      tv = CompareSqlValues(l, r, node.bin_op, aff);
      break;
    }
    case ExprKind::kSubquery:
      tv = EvalSubqueryTruth(node, row);
      break;
    default: {
      const SqlValue v = EvalValue(node, row);
      if (v.IsNull()) {
        tv = TruthValue::kUnknown;
        break;
      }
      const std::string s = v.ToLegacyString();
      tv = (s == "0") ? TruthValue::kFalse : TruthValue::kTrue;
      break;
    }
  }
  if (node.is_not) return Not3(tv);
  return tv;
}

bool ExprEval::EvalBool(const ExprNode& node, const RowMap& row) const {
  return IsSqlTrue(EvalTruth(node, row));
}

std::string ExprEval::EvalScalar(const ExprNode& node, const RowMap& row) const {
  return EvalValue(node, row).ToLegacyString();
}

}  // namespace sql
}  // namespace ebtree
