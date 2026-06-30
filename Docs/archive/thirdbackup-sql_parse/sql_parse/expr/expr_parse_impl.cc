#include "sql_parse/expr/expr_parse.h"

#include "common/parse_error.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <sstream>

#include "sql_parse/expr/sql_func_registry.h"
#include "sql_parse/shared/parse_shared.h"
#include "sql_parse/shared/subquery_attach.h"
#include "sql_parse/stmt/parse_statement.h"

namespace heterodb::sql_parse {
namespace {

std::string UpperToken(const std::string& s) {
  std::string u = s;
  for (char& c : u) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return u;
}

std::string Unquote(const std::string& s) {
  if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

std::string BareCol(const std::string& q) {
  const auto dot = q.find('.');
  return dot == std::string::npos ? q : q.substr(dot + 1);
}

bool IsCompareToken(const std::string& t) {
  return t == "=" || t == "!=" || t == "<>" || t == "<" || t == "<=" ||
         t == ">" || t == ">=";
}

ExprBinaryOp CompareToOp(const std::string& t) {
  if (t == "=") {
    return ExprBinaryOp::kEq;
  }
  if (t == "!=" || t == "<>") {
    return ExprBinaryOp::kNe;
  }
  if (t == "<") {
    return ExprBinaryOp::kLt;
  }
  if (t == "<=") {
    return ExprBinaryOp::kLe;
  }
  if (t == ">") {
    return ExprBinaryOp::kGt;
  }
  return ExprBinaryOp::kGe;
}

Status ParseExprInternal(const std::vector<std::string>& tokens, size_t* pos,
                         Expr** out, int min_prec);

bool TryParseInExpr(const std::vector<std::string>& tokens, size_t* pos, Expr* left,
                    Expr** out) {
  if (left == nullptr || left->kind != ExprKind::kColumn) {
    return false;
  }
  bool not_in = false;
  size_t p = *pos;
  if (p >= tokens.size()) {
    return false;
  }
  if (UpperToken(tokens[p]) == "NOT") {
    if (p + 1 >= tokens.size() || UpperToken(tokens[p + 1]) != "IN") {
      return false;
    }
    not_in = true;
    p += 2;
  } else if (UpperToken(tokens[p]) == "IN") {
    ++p;
  } else {
    return false;
  }
  if (p >= tokens.size() || tokens[p] != "(") {
    return false;
  }
  ++p;
  auto* fn = new Expr();
  fn->kind = ExprKind::kFunc;
  fn->func_name = not_in ? "__het_not_in" : "__het_in";
  fn->func_args.push_back(left);
  while (p < tokens.size() && tokens[p] != ")") {
    if (tokens[p] == ",") {
      ++p;
      continue;
    }
    Expr* arg = nullptr;
    if (!ParseExprInternal(tokens, &p, &arg, 0).ok() || arg == nullptr) {
      DeleteExpr(fn);
      return false;
    }
    fn->func_args.push_back(arg);
  }
  if (p >= tokens.size() || tokens[p] != ")") {
    DeleteExpr(fn);
    return false;
  }
  *pos = p + 1;
  *out = fn;
  return true;
}

Status ParsePrimary(const std::vector<std::string>& tokens, size_t* pos,
                    Expr** out) {
  if (*pos >= tokens.size()) {
    return Status::Syntax("incomplete expression", ParseErrorKind::kSyntax);
  }
  const std::string& tok = tokens[*pos];
  const std::string u = UpperToken(tok);

  if (tok.size() > 2 && (tok.rfind("0x", 0) == 0 || tok.rfind("0X", 0) == 0)) {
    auto* e = new Expr();
    e->kind = ExprKind::kLiteral;
    e->literal = SqlValue::FromInt(static_cast<int64_t>(
        std::strtoull(tok.c_str() + 2, nullptr, 16)));
    ++(*pos);
    *out = e;
    return Status::OK();
  }
  if (tok.size() > 2 && (tok.rfind("b'", 0) == 0 || tok.rfind("B'", 0) == 0) &&
      tok.back() == '\'') {
    auto* e = new Expr();
    e->kind = ExprKind::kLiteral;
    e->literal = SqlValue::FromInt(static_cast<int64_t>(
        std::strtoull(tok.c_str() + 2, nullptr, 2)));
    ++(*pos);
    *out = e;
    return Status::OK();
  }
  if (u == "NULL") {
    auto* e = new Expr();
    e->kind = ExprKind::kLiteral;
    e->literal = SqlValue::Null();
    ++(*pos);
    *out = e;
    return Status::OK();
  }
  if (u == "TRUE" || u == "FALSE") {
    auto* e = new Expr();
    e->kind = ExprKind::kLiteral;
    e->literal = SqlValue::FromBool(u == "TRUE");
    ++(*pos);
    *out = e;
    return Status::OK();
  }
  if (tok.front() == '\'') {
    auto* e = new Expr();
    e->kind = ExprKind::kLiteral;
    e->literal = SqlValue::FromString(Unquote(tok));
    ++(*pos);
    *out = e;
    return Status::OK();
  }
  if (!tok.empty() &&
      (std::isdigit(static_cast<unsigned char>(tok[0])) || tok[0] == '-' ||
       tok[0] == '+')) {
    auto* e = new Expr();
    e->kind = ExprKind::kLiteral;
    try {
      if (tok.find('.') != std::string::npos) {
        e->literal = SqlValue::FromDouble(std::stod(tok));
      } else {
        e->literal = SqlValue::FromInt(std::stoll(tok));
      }
    } catch (...) {
      DeleteExpr(e);
      return Status::Syntax("invalid numeric literal", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    *out = e;
    return Status::OK();
  }
  if (tok == "(") {
    if (*pos + 1 < tokens.size() && UpperToken(tokens[*pos + 1]) == "SELECT") {
      int depth = 0;
      const size_t start = *pos;
      size_t end = start;
      for (; end < tokens.size(); ++end) {
        if (tokens[end] == "(") {
          ++depth;
        } else if (tokens[end] == ")") {
          --depth;
          if (depth == 0) {
            break;
          }
        }
      }
      if (end >= tokens.size()) {
        return Status::Syntax("missing ) in scalar subquery", ParseErrorKind::kSyntax);
      }
      std::shared_ptr<SqlStatement> nested;
      const Status ps =
          AttachSelectSubqueryFromTokens(tokens, start + 1, end, &nested);
      if (!ps.ok()) {
        return ps;
      }
      auto* e = new Expr();
      e->kind = ExprKind::kScalarSubquery;
      e->scalar_subquery_stmt = std::move(nested);
      *pos = end + 1;
      *out = e;
      return Status::OK();
    }
    ++(*pos);
    Expr* inner = nullptr;
    Status s = ParseExprInternal(tokens, pos, &inner, 0);
    if (!s.ok()) {
      return s;
    }
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      DeleteExpr(inner);
      return Status::Syntax("expected ) in expression", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    *out = inner;
    return Status::OK();
  }
  if (u == "CASE") {
    auto* e = new Expr();
    e->kind = ExprKind::kCase;
    ++(*pos);
    while (*pos < tokens.size() && UpperToken(tokens[*pos]) == "WHEN") {
      ++(*pos);
      Expr* cond = nullptr;
      Status s = ParseExprInternal(tokens, pos, &cond, 0);
      if (!s.ok()) {
        DeleteExpr(e);
        return s;
      }
      if (*pos >= tokens.size() || UpperToken(tokens[*pos]) != "THEN") {
        DeleteExpr(cond);
        DeleteExpr(e);
        return Status::Syntax("CASE WHEN needs THEN", ParseErrorKind::kSyntax);
      }
      ++(*pos);
      Expr* val = nullptr;
      s = ParseExprInternal(tokens, pos, &val, 0);
      if (!s.ok()) {
        DeleteExpr(cond);
        DeleteExpr(e);
        return s;
      }
      e->case_when_conds.push_back(cond);
      e->case_then_vals.push_back(val);
    }
    if (*pos < tokens.size() && UpperToken(tokens[*pos]) == "ELSE") {
      ++(*pos);
      Status s = ParseExprInternal(tokens, pos, &e->case_else, 0);
      if (!s.ok()) {
        DeleteExpr(e);
        return s;
      }
    }
    if (*pos >= tokens.size() || UpperToken(tokens[*pos]) != "END") {
      DeleteExpr(e);
      return Status::Syntax("CASE needs END", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    *out = e;
    return Status::OK();
  }
  if (u == "CAST" || u == "CONVERT") {
    if (*pos + 1 >= tokens.size() || tokens[*pos + 1] != "(") {
      return Status::Syntax(u + " needs (", ParseErrorKind::kSyntax);
    }
    *pos += 2;
    auto* e = new Expr();
    e->kind = ExprKind::kCast;
    Status s = ParseExprInternal(tokens, pos, &e->cast_operand, 0);
    if (!s.ok()) {
      DeleteExpr(e);
      return s;
    }
    if (*pos >= tokens.size() || UpperToken(tokens[*pos]) != "AS") {
      DeleteExpr(e);
      return Status::Syntax("CAST needs AS", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size()) {
      DeleteExpr(e);
      return Status::Syntax("CAST needs type", ParseErrorKind::kSyntax);
    }
    const std::string ty = UpperToken(tokens[*pos]);
    if (ty == "INT" || ty == "INTEGER" || ty == "BIGINT") {
      e->cast_target = ExprCastTarget::kInt;
      ++(*pos);
    } else if (ty == "DOUBLE" || ty == "FLOAT") {
      e->cast_target = ExprCastTarget::kDouble;
      ++(*pos);
    } else if (ty == "BOOL" || ty == "BOOLEAN") {
      e->cast_target = ExprCastTarget::kBool;
      ++(*pos);
    } else if (ty == "DATE") {
      e->cast_target = ExprCastTarget::kDate;
      ++(*pos);
    } else if (ty == "TIMESTAMP" || ty == "DATETIME") {
      e->cast_target = ExprCastTarget::kTimestamp;
      ++(*pos);
    } else if (ty == "DECIMAL" || ty.rfind("DECIMAL", 0) == 0) {
      e->cast_target = ExprCastTarget::kDecimal;
      ++(*pos);
      if (*pos < tokens.size() && tokens[*pos] == "(") {
        ++(*pos);
        while (*pos < tokens.size() && tokens[*pos] != ")") {
          ++(*pos);
        }
        if (*pos < tokens.size() && tokens[*pos] == ")") {
          ++(*pos);
        }
      }
    } else if (ty == "VARCHAR" || ty.rfind("VARCHAR", 0) == 0) {
      e->cast_target = ExprCastTarget::kVarchar;
      ++(*pos);
      if (*pos < tokens.size() && tokens[*pos] == "(") {
        ++(*pos);
        while (*pos < tokens.size() && tokens[*pos] != ")") {
          ++(*pos);
        }
        if (*pos < tokens.size() && tokens[*pos] == ")") {
          ++(*pos);
        }
      }
    } else {
      e->cast_target = ExprCastTarget::kString;
      ++(*pos);
    }
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      DeleteExpr(e);
      return Status::Syntax("CAST missing )", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    *out = e;
    return Status::OK();
  }
  if (u == "EXTRACT") {
    if (*pos + 1 >= tokens.size() || tokens[*pos + 1] != "(") {
      return Status::Syntax("EXTRACT needs (", ParseErrorKind::kSyntax);
    }
    *pos += 2;
    if (*pos >= tokens.size()) {
      return Status::Syntax("EXTRACT needs field", ParseErrorKind::kSyntax);
    }
    const std::string field = UpperToken(tokens[(*pos)++]);
    if (*pos >= tokens.size() || UpperToken(tokens[*pos]) != "FROM") {
      return Status::Syntax("EXTRACT needs FROM", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    Expr* source = nullptr;
    Status s = ParseExprInternal(tokens, pos, &source, 0);
    if (!s.ok()) {
      return s;
    }
    auto* field_lit = new Expr();
    field_lit->kind = ExprKind::kColumn;
    field_lit->column = field;
    auto* from_lit = new Expr();
    from_lit->kind = ExprKind::kColumn;
    from_lit->column = "FROM";
    auto* e = new Expr();
    e->kind = ExprKind::kFunc;
    e->func_name = "EXTRACT";
    e->func_args.push_back(field_lit);
    e->func_args.push_back(from_lit);
    e->func_args.push_back(source);
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      DeleteExpr(e);
      return Status::Syntax("missing ) after EXTRACT", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    *out = e;
    return Status::OK();
  }
  if (u == "DATE_ADD" || u == "DATE_SUB") {
    if (*pos + 1 >= tokens.size() || tokens[*pos + 1] != "(") {
      return Status::Syntax(u + " needs (", ParseErrorKind::kSyntax);
    }
    *pos += 2;
    auto* e = new Expr();
    e->kind = ExprKind::kFunc;
    e->func_name = u;
    Expr* date = nullptr;
    Status s = ParseExprInternal(tokens, pos, &date, 0);
    if (!s.ok()) {
      DeleteExpr(e);
      return s;
    }
    e->func_args.push_back(date);
    if (*pos < tokens.size() && tokens[*pos] == ",") {
      ++(*pos);
    }
    if (*pos < tokens.size() && UpperToken(tokens[*pos]) == "INTERVAL") {
      ++(*pos);
      Expr* n = nullptr;
      s = ParseExprInternal(tokens, pos, &n, 0);
      if (!s.ok()) {
        DeleteExpr(e);
        return s;
      }
      e->func_args.push_back(n);
      if (*pos >= tokens.size()) {
        DeleteExpr(e);
        return Status::Syntax("INTERVAL requires unit", ParseErrorKind::kSyntax);
      }
      auto* unit = new Expr();
      unit->kind = ExprKind::kLiteral;
      unit->literal = SqlValue::FromString(UpperToken(tokens[(*pos)++]));
      e->func_args.push_back(unit);
    } else {
      Expr* off = nullptr;
      s = ParseExprInternal(tokens, pos, &off, 0);
      if (!s.ok()) {
        DeleteExpr(e);
        return s;
      }
      e->func_args.push_back(off);
    }
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      DeleteExpr(e);
      return Status::Syntax("missing ) after " + u, ParseErrorKind::kSyntax);
    }
    ++(*pos);
    *out = e;
    return Status::OK();
  }
  if (*pos + 1 < tokens.size() && tokens[*pos + 1] == "(" &&
      IsSqlParseSupportedFunction(u) &&
      (!SqlFunctionIsAggregate(u) || u == "GROUP_CONCAT") &&
      u != "EXTRACT" && u != "DATE_ADD" && u != "DATE_SUB" && u != "CAST" &&
      u != "CONVERT") {
    auto* e = new Expr();
    e->kind = ExprKind::kFunc;
    e->func_name = u;
    *pos += 2;
    if (*pos < tokens.size() && tokens[*pos] != ")") {
      for (;;) {
        Expr* arg = nullptr;
        Status s = ParseExprInternal(tokens, pos, &arg, 0);
        if (!s.ok()) {
          DeleteExpr(e);
          return s;
        }
        e->func_args.push_back(arg);
        if (*pos < tokens.size() && tokens[*pos] == ",") {
          ++(*pos);
          continue;
        }
        break;
      }
    }
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      DeleteExpr(e);
      return Status::Syntax("missing ) after function call", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    *out = e;
    return Status::OK();
  }
  if (u == "COALESCE" || u == "UPPER" || u == "LOWER" || u == "CONCAT" ||
      u == "NULLIF" || u == "JSON_EXTRACT" || u == "SUBSTRING" ||
      u == "IFNULL" || u == "DATE" || u == "DATE_FORMAT" || u == "YEAR" ||
      u == "MONTH" || u == "DAY" ||
      u == "TRIM" || u == "LTRIM" || u == "RTRIM" ||
      u == "LENGTH" || u == "CHAR_LENGTH" || u == "ABS" || u == "ROUND" ||
      u == "CAST" || u == "COUNT" || u == "SUM" || u == "AVG" || u == "MIN" ||
      u == "MAX") {
    if (*pos + 1 >= tokens.size() || tokens[*pos + 1] != "(") {
      return Status::Syntax(u + " needs (", ParseErrorKind::kSyntax);
    }
  auto* e = new Expr();
    e->kind = ExprKind::kFunc;
    e->func_name = u;
    *pos += 2;
    if (*pos < tokens.size() && tokens[*pos] != ")") {
      for (;;) {
        Expr* arg = nullptr;
        Status s = ParseExprInternal(tokens, pos, &arg, 0);
        if (!s.ok()) {
          DeleteExpr(e);
          return s;
        }
        e->func_args.push_back(arg);
        if (*pos < tokens.size() && tokens[*pos] == ",") {
          ++(*pos);
          continue;
        }
        break;
      }
    }
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      DeleteExpr(e);
      return Status::Syntax("missing ) after function call", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    *out = e;
    return Status::OK();
  }

  if (*pos + 1 < tokens.size() && tokens[*pos + 1] == "(" &&
      !IsSqlParseSupportedFunction(u)) {
    return Status::Syntax("unsupported function: " + u,
                        ParseErrorKind::kInvalidFunction);
  }

  auto* e = new Expr();
  e->kind = ExprKind::kColumn;
  const auto dot = tok.find('.');
  if (dot != std::string::npos) {
    e->table_qual = tok.substr(0, dot);
    e->column = tok.substr(dot + 1);
  } else {
    e->column = tok;
  }
  ++(*pos);
  *out = e;
  return Status::OK();
}

int Prec(ExprBinaryOp op) {
  switch (op) {
    case ExprBinaryOp::kOr:
      return 1;
    case ExprBinaryOp::kAnd:
      return 2;
    case ExprBinaryOp::kEq:
    case ExprBinaryOp::kNe:
    case ExprBinaryOp::kLt:
    case ExprBinaryOp::kLe:
    case ExprBinaryOp::kGt:
    case ExprBinaryOp::kGe:
      return 3;
    case ExprBinaryOp::kAdd:
    case ExprBinaryOp::kSub:
      return 4;
    case ExprBinaryOp::kMul:
    case ExprBinaryOp::kDiv:
      return 5;
    default:
      return 0;
  }
}

Status ParseExprInternal(const std::vector<std::string>& tokens, size_t* pos,
                         Expr** out, int min_prec) {
  if (*pos >= tokens.size()) {
    return Status::Syntax("incomplete expression", ParseErrorKind::kSyntax);
  }
  if (UpperToken(tokens[*pos]) == "NOT") {
    ++(*pos);
    Expr* operand = nullptr;
    Status s = ParseExprInternal(tokens, pos, &operand, 6);
    if (!s.ok()) {
      return s;
    }
    auto* e = new Expr();
    e->kind = ExprKind::kUnary;
    e->unary_op = ExprUnaryOp::kNot;
    e->unary_operand = operand;
    *out = e;
    return Status::OK();
  }
  if (tokens[*pos] == "-" &&
      (*pos + 1 < tokens.size()) &&
      (tokens[*pos + 1].front() == '\'' ||
       std::isdigit(static_cast<unsigned char>(tokens[*pos + 1][0])) ||
       tokens[*pos + 1] == "(")) {
    ++(*pos);
    Expr* operand = nullptr;
    Status s = ParseExprInternal(tokens, pos, &operand, 6);
    if (!s.ok()) {
      return s;
    }
    auto* e = new Expr();
    e->kind = ExprKind::kUnary;
    e->unary_op = ExprUnaryOp::kNeg;
    e->unary_operand = operand;
    *out = e;
    return Status::OK();
  }

  Expr* left = nullptr;
  Status s = ParsePrimary(tokens, pos, &left);
  if (!s.ok()) {
    return s;
  }

  while (*pos < tokens.size()) {
    if (*pos < tokens.size() &&
        (tokens[*pos] == "->" || tokens[*pos] == "->>")) {
      const bool unquote = tokens[*pos] == "->>";
      ++(*pos);
      if (*pos >= tokens.size()) {
        DeleteExpr(left);
        return Status::Syntax("JSON path expected after ->", ParseErrorKind::kSyntax);
      }
      std::string path = Unquote(tokens[(*pos)++]);
      auto* path_lit = new Expr();
      path_lit->kind = ExprKind::kLiteral;
      path_lit->literal = SqlValue::FromString(path);
      auto* e = new Expr();
      e->kind = ExprKind::kBinary;
      e->binary_op =
          unquote ? ExprBinaryOp::kJsonArrowUnquote : ExprBinaryOp::kJsonArrow;
      e->binary_left = left;
      e->binary_right = path_lit;
      left = e;
      continue;
    }
    {
      Expr* in_expr = nullptr;
      if (TryParseInExpr(tokens, pos, left, &in_expr)) {
        left = in_expr;
        continue;
      }
    }
    if (UpperToken(tokens[*pos]) == "AND") {
      if (Prec(ExprBinaryOp::kAnd) < min_prec) {
        break;
      }
      ++(*pos);
      Expr* right = nullptr;
      s = ParseExprInternal(tokens, pos, &right, Prec(ExprBinaryOp::kAnd));
      if (!s.ok()) {
        DeleteExpr(left);
        return s;
      }
      auto* e = new Expr();
      e->kind = ExprKind::kBinary;
      e->binary_op = ExprBinaryOp::kAnd;
      e->binary_left = left;
      e->binary_right = right;
      left = e;
      continue;
    }
    if (UpperToken(tokens[*pos]) == "OR") {
      if (Prec(ExprBinaryOp::kOr) < min_prec) {
        break;
      }
      ++(*pos);
      Expr* right = nullptr;
      s = ParseExprInternal(tokens, pos, &right, Prec(ExprBinaryOp::kOr));
      if (!s.ok()) {
        DeleteExpr(left);
        return s;
      }
      auto* e = new Expr();
      e->kind = ExprKind::kBinary;
      e->binary_op = ExprBinaryOp::kOr;
      e->binary_left = left;
      e->binary_right = right;
      left = e;
      continue;
    }
    if (IsCompareToken(tokens[*pos])) {
      const ExprBinaryOp op = CompareToOp(tokens[*pos]);
      if (Prec(op) < min_prec) {
        break;
      }
      ++(*pos);
      Expr* right = nullptr;
      s = ParseExprInternal(tokens, pos, &right, Prec(op) + 1);
      if (!s.ok()) {
        DeleteExpr(left);
        return s;
      }
      auto* e = new Expr();
      e->kind = ExprKind::kBinary;
      e->binary_op = op;
      e->binary_left = left;
      e->binary_right = right;
      left = e;
      continue;
    }
    if (tokens[*pos] == "+" || tokens[*pos] == "-" || tokens[*pos] == "*" ||
        tokens[*pos] == "/") {
      ExprBinaryOp op = ExprBinaryOp::kAdd;
      if (tokens[*pos] == "-") {
        op = ExprBinaryOp::kSub;
      } else if (tokens[*pos] == "*") {
        op = ExprBinaryOp::kMul;
      } else if (tokens[*pos] == "/") {
        op = ExprBinaryOp::kDiv;
      }
      if (Prec(op) < min_prec) {
        break;
      }
      ++(*pos);
      Expr* right = nullptr;
      s = ParseExprInternal(tokens, pos, &right, Prec(op) + 1);
      if (!s.ok()) {
        DeleteExpr(left);
        return s;
      }
      auto* e = new Expr();
      e->kind = ExprKind::kBinary;
      e->binary_op = op;
      e->binary_left = left;
      e->binary_right = right;
      left = e;
      continue;
    }
    break;
  }
  *out = left;
  return Status::OK();
}

bool IsExprStopTokenImpl(const std::string& tok) {
  const std::string u = UpperToken(tok);
  return u == "FROM" || u == "WHERE" || u == "GROUP" || u == "ORDER" ||
         u == "HAVING" || u == "LIMIT" || u == "OFFSET" || u == "UNION" ||
         u == "JOIN" || u == ";" || u == "," || u == ")";
}

}  // namespace

Status ParseExpr(const std::vector<std::string>& tokens, size_t* pos,
                 Expr** out) {
  return ParseExprInternal(tokens, pos, out, 0);
}

bool IsExprStopToken(const std::string& tok) { return IsExprStopTokenImpl(tok); }

}  // namespace heterodb::sql_parse

