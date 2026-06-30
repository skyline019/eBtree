// ParseConcept | CHECK constraint expression parsing.
#include "sql_parse/stmt/ddl/check_api.h"

#include "common/parse_error.h"

#include "sql_parse/expr/expr_parse.h"
#include "concept/catalog/constraint_checker.h"
#include "sql_parse/shared/parse_shared.h"
#include "sql_parse/stmt/where/where_api.h"

#include "concept/query/expr.h"

#include <sstream>

namespace heterodb::sql_parse {
namespace detail {
namespace {

std::string JoinTokens(const std::vector<std::string>& tokens, size_t begin,
                       size_t end) {
  std::ostringstream oss;
  for (size_t i = begin; i < end; ++i) {
    if (i > begin) {
      oss << ' ';
    }
    oss << tokens[i];
  }
  return oss.str();
}

Status ParseLegacyCheckBody(const std::vector<std::string>& tokens, size_t* pos,
                            CheckConstraintDef* chk) {
  if (*pos + 2 >= tokens.size()) {
    return Status::Syntax("CHECK needs column op value", ParseErrorKind::kSyntax);
  }
  chk->column = tokens[(*pos)++];
  Status cs = ParseCompareOp(tokens[(*pos)++], &chk->op);
  if (!cs.ok()) {
    return cs;
  }
  if (chk->op == CompareOp::kIn) {
    if (*pos >= tokens.size() || tokens[*pos] != "(") {
      return Status::Syntax("IN ( values ) expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    while (*pos < tokens.size() && tokens[*pos] != ")") {
      if (tokens[*pos] != ",") {
        chk->in_values.push_back(Unquote(tokens[(*pos)++]));
      } else {
        ++(*pos);
      }
    }
    ++(*pos);
  } else {
    chk->value = Unquote(tokens[(*pos)++]);
  }
  return Status::OK();
}

}  // namespace

std::string JoinCheckExprTokens(const std::vector<std::string>& tokens, size_t begin,
                                size_t end) {
  std::ostringstream oss;
  for (size_t i = begin; i < end; ++i) {
    if (i > begin) {
      oss << '\x1f';
    }
    oss << tokens[i];
  }
  return oss.str();
}

Status ParseCheckConstraintBody(const std::vector<std::string>& tokens, size_t* pos,
                                const std::string& name, CheckConstraintDef* chk) {
  if (chk == nullptr) {
    return Status::InvalidArgument("null check output");
  }
  if (*pos >= tokens.size() || tokens[*pos] != "(") {
    return Status::Syntax("CHECK ( expr ) expected", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  const size_t body_start = *pos;
  int depth = 1;
  while (*pos < tokens.size() && depth > 0) {
    if (tokens[*pos] == "(") {
      ++depth;
    } else if (tokens[*pos] == ")") {
      --depth;
      if (depth == 0) {
        break;
      }
    }
    ++(*pos);
  }
  if (depth != 0) {
    return Status::Syntax("CHECK missing )", ParseErrorKind::kSyntax);
  }
  const size_t body_end = *pos;
  std::vector<std::string> slice(tokens.begin() + static_cast<std::ptrdiff_t>(body_start),
                                 tokens.begin() + static_cast<std::ptrdiff_t>(body_end));

  chk->name = name;
  chk->column.clear();
  chk->op = CompareOp::kEq;
  chk->value.clear();
  chk->value2.clear();
  chk->in_values.clear();
  chk->check_expr_sql.clear();

  size_t spos = 0;
  Expr* expr = nullptr;
  if (::heterodb::sql_parse::ParseExpr(slice, &spos, &expr).ok() && expr != nullptr &&
      spos == slice.size()) {
    chk->check_expr_sql = JoinCheckExprTokens(slice, 0, slice.size());
    chk->check_expr_ast.reset(CloneExprTree(expr));
    DeleteExpr(expr);
    EnsureCheckConstraintAst(chk);
    ++(*pos);
    return Status::OK();
  }
  DeleteExpr(expr);

  *pos = body_start;
  Status legacy = ParseLegacyCheckBody(tokens, pos, chk);
  if (!legacy.ok()) {
    return legacy;
  }
  chk->check_expr_sql = JoinCheckExprTokens(slice, 0, slice.size());
  EnsureCheckConstraintAst(chk);
  if (*pos >= tokens.size() || tokens[*pos] != ")") {
    return Status::Syntax("CHECK missing )", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  return Status::OK();
}

}  // namespace detail
}  // namespace heterodb::sql_parse
