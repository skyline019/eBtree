#include "sql_parse/pred/where_lower.h"

#include "sql_parse/stmt/parse_common_api.h"
#include "sql_parse/pred/where_normalize.h"
#include "sql_parse/stmt/where/where_api.h"

#include <memory>
#include <vector>

namespace heterodb::sql_parse::pred {
namespace {

Expr* MakeLit(const std::string& raw) {
  auto* e = new Expr();
  e->kind = ExprKind::kLiteral;
  e->literal = SqlValue::FromString(detail::Unquote(raw));
  return e;
}

Expr* MakeCol(const std::string& col) {
  auto* e = new Expr();
  e->kind = ExprKind::kColumn;
  const auto dot = col.find('.');
  if (dot != std::string::npos) {
    e->table_qual = col.substr(0, dot);
    e->column = col.substr(dot + 1);
  } else {
    e->column = col;
  }
  return e;
}

Expr* MakeBin(ExprBinaryOp op, Expr* l, Expr* r) {
  auto* e = new Expr();
  e->kind = ExprKind::kBinary;
  e->binary_op = op;
  e->binary_left = l;
  e->binary_right = r;
  return e;
}

Expr* MakeAnd(Expr* a, Expr* b) {
  if (a == nullptr) {
    return b;
  }
  if (b == nullptr) {
    return a;
  }
  return MakeBin(ExprBinaryOp::kAnd, a, b);
}

ExprBinaryOp CompareToBin(CompareOp op) {
  switch (op) {
    case CompareOp::kEq:
      return ExprBinaryOp::kEq;
    case CompareOp::kNe:
      return ExprBinaryOp::kNe;
    case CompareOp::kLt:
      return ExprBinaryOp::kLt;
    case CompareOp::kLe:
      return ExprBinaryOp::kLe;
    case CompareOp::kGt:
      return ExprBinaryOp::kGt;
    case CompareOp::kGe:
      return ExprBinaryOp::kGe;
    default:
      return ExprBinaryOp::kEq;
  }
}

CompareOp BinToCompare(ExprBinaryOp op) {
  switch (op) {
    case ExprBinaryOp::kEq:
      return CompareOp::kEq;
    case ExprBinaryOp::kNe:
      return CompareOp::kNe;
    case ExprBinaryOp::kLt:
      return CompareOp::kLt;
    case ExprBinaryOp::kLe:
      return CompareOp::kLe;
    case ExprBinaryOp::kGt:
      return CompareOp::kGt;
    case ExprBinaryOp::kGe:
      return CompareOp::kGe;
    default:
      return CompareOp::kEq;
  }
}

Expr* PredicateToExpr(const ColumnPredicate& cp) {
  switch (cp.op) {
    case CompareOp::kIsNull:
    case CompareOp::kIsNotNull:
    case CompareOp::kIsUnknown:
    case CompareOp::kIsNotUnknown: {
      auto* fn = new Expr();
      fn->kind = ExprKind::kFunc;
      fn->func_name = "__het_pred";
      fn->func_args.push_back(MakeCol(cp.column));
      fn->func_args.push_back(MakeLit(cp.op == CompareOp::kIsNull
                                          ? "'__is_null__'"
                                          : cp.op == CompareOp::kIsNotNull
                                                ? "'__is_not_null__'"
                                                : cp.op == CompareOp::kIsUnknown
                                                      ? "'__is_unknown__'"
                                                      : "'__is_not_unknown__'"));
      return fn;
    }
    case CompareOp::kIn:
    case CompareOp::kNotIn: {
      auto* fn = new Expr();
      fn->kind = ExprKind::kFunc;
      fn->func_name = cp.op == CompareOp::kNotIn ? "__het_not_in" : "__het_in";
      fn->func_args.push_back(MakeCol(cp.column));
      for (const auto& v : cp.in_values) {
        fn->func_args.push_back(MakeLit("'" + v + "'"));
      }
      return fn;
    }
    case CompareOp::kInSubquery:
    case CompareOp::kNotInSubquery:
    case CompareOp::kAnySubquery:
    case CompareOp::kAllSubquery:
    case CompareOp::kExists:
    case CompareOp::kNotExists: {
      auto* fn = new Expr();
      fn->kind = ExprKind::kFunc;
      fn->func_name = "__het_subq_pred";
      fn->func_args.push_back(MakeCol(cp.column));
      fn->func_args.push_back(MakeLit(cp.subquery_stmt ? "'1'" : "''"));
      fn->func_args.push_back(MakeLit(std::to_string(static_cast<int>(cp.op))));
      if (cp.op == CompareOp::kAnySubquery || cp.op == CompareOp::kAllSubquery) {
        fn->func_args.push_back(
            MakeLit(std::to_string(static_cast<int>(cp.quantifier_cmp))));
      }
      return fn;
    }
    case CompareOp::kLike:
    case CompareOp::kNotLike:
    case CompareOp::kRegexp: {
      auto* fn = new Expr();
      fn->kind = ExprKind::kFunc;
      fn->func_name = "__het_like_pred";
      fn->func_args.push_back(MakeCol(cp.column));
      fn->func_args.push_back(MakeLit("'" + cp.value + "'"));
      fn->func_args.push_back(MakeLit(std::to_string(static_cast<int>(cp.op))));
      return fn;
    }
    default:
      if (cp.op == CompareOp::kGe && !cp.value2.empty()) {
        return MakeAnd(
            MakeBin(ExprBinaryOp::kGe, MakeCol(cp.column), MakeLit("'" + cp.value + "'")),
            MakeBin(ExprBinaryOp::kLe, MakeCol(cp.column), MakeLit("'" + cp.value2 + "'")));
      }
      if (cp.scalar_subquery && cp.subquery_stmt != nullptr) {
        auto* sq = new Expr();
        sq->kind = ExprKind::kScalarSubquery;
        sq->scalar_subquery_stmt = cp.subquery_stmt;
        return MakeBin(CompareToBin(cp.op), MakeCol(cp.column), sq);
      }
      return MakeBin(CompareToBin(cp.op), MakeCol(cp.column), MakeLit("'" + cp.value + "'"));
  }
}

Expr* BuildWhereExprFromPredicates(const SqlStatement& stmt) {
  Expr* root = nullptr;
  for (const auto& cp : stmt.where_and) {
    root = MakeAnd(root, PredicateToExpr(cp));
  }
  if (!stmt.where_and.empty()) {
    return root;
  }
  for (const auto& or_clause : stmt.where_or) {
    Expr* or_root = nullptr;
    for (const auto& cp : or_clause) {
      or_root = MakeAnd(or_root, PredicateToExpr(cp));
    }
    if (or_root == nullptr) {
      continue;
    }
    if (root == nullptr) {
      root = or_root;
    } else {
      root = MakeBin(ExprBinaryOp::kOr, root, or_root);
    }
  }
  return root;
}

bool TryLowerCompare(Expr* left, Expr* right, CompareOp op,
                     std::vector<ColumnPredicate>* out) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  if (left->kind == ExprKind::kColumn && right->kind == ExprKind::kLiteral) {
    ColumnPredicate cp;
    cp.column = left->column;
    if (!left->table_qual.empty()) {
      cp.column = left->table_qual + "." + left->column;
    }
    cp.op = op;
    cp.value = right->literal.ToSqlString();
    out->push_back(cp);
    return true;
  }
  if (left->kind == ExprKind::kLiteral && right->kind == ExprKind::kColumn) {
    ColumnPredicate cp;
    cp.column = right->column;
    if (!right->table_qual.empty()) {
      cp.column = right->table_qual + "." + right->column;
    }
    cp.op = op;
    cp.value = left->literal.ToSqlString();
    out->push_back(cp);
    return true;
  }
  if (left->kind == ExprKind::kScalarSubquery && right->kind == ExprKind::kColumn) {
    ColumnPredicate cp;
    cp.column = right->column;
    if (!right->table_qual.empty()) {
      cp.column = right->table_qual + "." + right->column;
    }
    cp.op = op;
    cp.scalar_subquery = true;
    cp.subquery_stmt = left->scalar_subquery_stmt;
    out->push_back(cp);
    return true;
  }
  if (left->kind == ExprKind::kColumn && right->kind == ExprKind::kScalarSubquery) {
    ColumnPredicate cp;
    cp.column = left->column;
    if (!left->table_qual.empty()) {
      cp.column = left->table_qual + "." + left->column;
    }
    cp.op = op;
    cp.scalar_subquery = true;
    cp.subquery_stmt = right->scalar_subquery_stmt;
    out->push_back(cp);
    return true;
  }
  return false;
}

bool LowerExprToPredicates(Expr* e, std::vector<ColumnPredicate>* out) {
  if (e == nullptr) {
    return true;
  }
  if (e->kind == ExprKind::kBinary && e->binary_op == ExprBinaryOp::kAnd) {
    std::vector<ColumnPredicate> left;
    std::vector<ColumnPredicate> right;
    if (!LowerExprToPredicates(e->binary_left, &left) ||
        !LowerExprToPredicates(e->binary_right, &right)) {
      return false;
    }
    if (left.size() == 1 && right.size() == 1 && left[0].column == right[0].column &&
        left[0].op == CompareOp::kGe && right[0].op == CompareOp::kLe) {
      ColumnPredicate between = left[0];
      between.value2 = right[0].value;
      out->push_back(between);
      return true;
    }
    out->insert(out->end(), left.begin(), left.end());
    out->insert(out->end(), right.begin(), right.end());
    return true;
  }
  if (e->kind == ExprKind::kBinary) {
    return TryLowerCompare(e->binary_left, e->binary_right, BinToCompare(e->binary_op),
                           out);
  }
  if (e->kind == ExprKind::kFunc && e->func_name == "__het_in") {
    if (e->func_args.empty() || e->func_args[0]->kind != ExprKind::kColumn) {
      return false;
    }
    ColumnPredicate cp;
    cp.column = e->func_args[0]->column;
    cp.op = CompareOp::kIn;
    for (size_t i = 1; i < e->func_args.size(); ++i) {
      cp.in_values.push_back(e->func_args[i]->literal.ToSqlString());
    }
    out->push_back(cp);
    return true;
  }
  if (e->kind == ExprKind::kFunc && e->func_name == "__het_not_in") {
    if (e->func_args.empty() || e->func_args[0]->kind != ExprKind::kColumn) {
      return false;
    }
    ColumnPredicate cp;
    cp.column = e->func_args[0]->column;
    cp.op = CompareOp::kNotIn;
    for (size_t i = 1; i < e->func_args.size(); ++i) {
      cp.in_values.push_back(e->func_args[i]->literal.ToSqlString());
    }
    out->push_back(cp);
    return true;
  }
  return false;
}

void LowerWhereExpr(SqlStatement* stmt) {
  if (stmt == nullptr || stmt->where_expr == nullptr) {
    return;
  }
  std::vector<ColumnPredicate> lowered;
  if (!LowerExprToPredicates(stmt->where_expr.get(), &lowered) || lowered.empty()) {
    return;
  }
  stmt->where_and = std::move(lowered);
  detail::SyncKeyScanRange(stmt);
}

void BuildWhereExprIfMissing(SqlStatement* stmt) {
  if (stmt == nullptr || stmt->where_expr != nullptr) {
    return;
  }
  if (stmt->where_and.empty() && stmt->where_or.empty() && stmt->where.has_value()) {
    ColumnPredicate cp;
    cp.column = stmt->where->column;
    cp.op = stmt->where->op;
    cp.value = stmt->where->value;
    stmt->where_and.push_back(cp);
  }
  if (stmt->where_and.empty() && stmt->where_or.empty()) {
    return;
  }
  Expr* built = BuildWhereExprFromPredicates(*stmt);
  if (built != nullptr) {
    stmt->where_expr.reset(built);
  }
}

Expr* BuildHavingExprFromPredicates(const SqlStatement& stmt) {
  Expr* root = nullptr;
  for (const auto& cp : stmt.having_and) {
    root = MakeAnd(root, PredicateToExpr(cp));
  }
  return root;
}

void BuildHavingExprIfMissing(SqlStatement* stmt) {
  if (stmt == nullptr || stmt->having_expr != nullptr) {
    return;
  }
  if (stmt->having_and.empty()) {
    return;
  }
  Expr* built = BuildHavingExprFromPredicates(*stmt);
  if (built != nullptr) {
    stmt->having_expr.reset(built);
  }
}

void LowerHavingExpr(SqlStatement* stmt) {
  if (stmt == nullptr || stmt->having_expr == nullptr) {
    return;
  }
  std::vector<ColumnPredicate> lowered;
  if (!LowerExprToPredicates(stmt->having_expr.get(), &lowered) || lowered.empty()) {
    return;
  }
  stmt->having_and = std::move(lowered);
}

}  // namespace

void ApplyWhereCanonicalize(SqlStatement* stmt) {
  if (stmt == nullptr) {
    return;
  }
  BuildWhereExprIfMissing(stmt);
  if (stmt->where_and.empty()) {
    LowerWhereExpr(stmt);
  }
  ApplyWhereNormalize(stmt);
}

void ApplyHavingCanonicalize(SqlStatement* stmt) {
  if (stmt == nullptr) {
    return;
  }
  BuildHavingExprIfMissing(stmt);
  if (stmt->having_and.empty()) {
    LowerHavingExpr(stmt);
  }
  ApplyHavingNormalize(stmt);
}

}  // namespace heterodb::sql_parse::pred
