#include "expr_parse.h"

#include "sql/parse/core/lex_pipeline.h"
#include "sql/parse/native/select_parse.h"
#include "sql/parse/shared/parse_shared.h"
#include <cstdlib>

namespace ebtree {
namespace sql {
namespace parse {

namespace {

std::unique_ptr<ExprNode> MakeLiteral(const std::string& v) {
  auto n = std::make_unique<ExprNode>();
  n->kind = ExprKind::kLiteral;
  n->literal = v;
  return n;
}

std::unique_ptr<ExprNode> MakeColumn(const std::string& table,
                                     const std::string& col) {
  auto n = std::make_unique<ExprNode>();
  n->kind = ExprKind::kColumn;
  n->table = table;
  n->column = col;
  return n;
}

std::unique_ptr<ExprNode> MakeBinary(BinaryOp op, std::unique_ptr<ExprNode> l,
                                     std::unique_ptr<ExprNode> r) {
  auto n = std::make_unique<ExprNode>();
  n->kind = ExprKind::kBinary;
  n->bin_op = op;
  n->children.push_back(std::move(l));
  n->children.push_back(std::move(r));
  return n;
}

bool IsClauseKeyword(const std::string& u) {
  return u == "AND" || u == "OR" || u == "GROUP" || u == "ORDER" ||
         u == "LIMIT" || u == "HAVING" || u == "JOIN" || u == "LEFT" ||
         u == "RIGHT" || u == "INNER" || u == "ON";
}

bool IsNumericToken(const std::string& tok) {
  if (tok.empty()) return false;
  size_t i = 0;
  if (tok[i] == '-' || tok[i] == '+') {
    if (tok.size() == 1) return false;
    ++i;
  }
  bool any = false;
  for (; i < tok.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(tok[i]))) return false;
    any = true;
  }
  return any;
}

bool IsNumericOrRealToken(const std::string& tok) {
  if (IsNumericToken(tok)) return true;
  if (tok.empty()) return false;
  char* end = nullptr;
  std::strtod(tok.c_str(), &end);
  return end != tok.c_str() && *end == '\0';
}

std::unique_ptr<ExprNode> CloneExpr(const ExprNode& node) {
  auto n = std::make_unique<ExprNode>();
  n->kind = node.kind;
  n->literal = node.literal;
  n->column = node.column;
  n->table = node.table;
  n->bin_op = node.bin_op;
  n->func_name = node.func_name;
  n->is_null_check = node.is_null_check;
  n->is_not = node.is_not;
  for (const auto& ch : node.children) {
    n->children.push_back(CloneExpr(*ch));
  }
  return n;
}

bool StartsWithSelectKeyword(const std::string& sql) {
  const std::string u = Upper(Trim(sql));
  return u.rfind("SELECT", 0) == 0;
}

}  // namespace

std::string ExprParse::ExtractBalancedParenSql(TokenCursor* cur) {
  if (cur->Peek() != "(") return {};
  cur->Consume(nullptr);
  size_t depth = 1;
  std::string sql;
  while (!cur->AtEnd() && depth > 0) {
    const std::string& t = cur->Peek();
    if (t == "(") ++depth;
    if (t == ")") {
      --depth;
      if (depth == 0) {
        cur->Consume(nullptr);
        break;
      }
    }
    if (!sql.empty()) sql.push_back(' ');
    sql += t;
    cur->Consume(nullptr);
  }
  return sql;
}

Status ExprParse::ParsePrimary(TokenCursor* cur, std::unique_ptr<ExprNode>* out) {
  if (!cur || !out) return Status::InvalidArgument("null argument");
  if (cur->AtEnd()) return Status::InvalidArgument("empty expression");

  const std::string u = cur->PeekUpper();
  if (u == "NOT") {
    cur->Consume(nullptr);
    if (cur->PeekUpper() == "EXISTS") {
      cur->Consume(nullptr);
      SubquerySpec sq{};
      sq.exists = true;
      sq.not_op = true;
      sq.sql = ExtractBalancedParenSql(cur);
      SelectQuery inner{};
      TokenCursor inner_cur(TokenizeSql(sq.sql));
      SelectParse sp;
      if (sp.ParseSelect(sq.sql, &inner_cur, &inner).ok()) {
        sq.parsed_query = std::make_unique<SelectQuery>(std::move(inner));
      }
      auto n = std::make_unique<ExprNode>();
      n->kind = ExprKind::kSubquery;
      n->subquery = std::move(sq);
      *out = std::move(n);
      return Status::Ok();
    }
    std::unique_ptr<ExprNode> inner;
    const Status ps = ParsePrimary(cur, &inner);
    if (!ps.ok()) return ps;
    inner->is_not = true;
    *out = std::move(inner);
    return Status::Ok();
  }

  if (u == "EXISTS") {
    cur->Consume(nullptr);
    SubquerySpec sq{};
    sq.exists = true;
    sq.sql = ExtractBalancedParenSql(cur);
    SelectQuery inner{};
    TokenCursor inner_cur(TokenizeSql(sq.sql));
    SelectParse sp;
    if (sp.ParseSelect(sq.sql, &inner_cur, &inner).ok()) {
      sq.parsed_query = std::make_unique<SelectQuery>(std::move(inner));
    }
    auto n = std::make_unique<ExprNode>();
    n->kind = ExprKind::kSubquery;
    n->subquery = std::move(sq);
    *out = std::move(n);
    return Status::Ok();
  }

  if (u == "NULL") {
    cur->Consume(nullptr);
    *out = MakeLiteral("");
    return Status::Ok();
  }

  if (cur->Peek() == "(") {
    const std::string inner = ExtractBalancedParenSql(cur);
    if (StartsWithSelectKeyword(inner)) {
      SubquerySpec sq{};
      sq.scalar = true;
      sq.sql = inner;
      SelectQuery parsed{};
      TokenCursor ic(TokenizeSql(inner));
      SelectParse sp;
      if (sp.ParseSelect(inner, &ic, &parsed).ok()) {
        sq.parsed_query = std::make_unique<SelectQuery>(std::move(parsed));
      }
      auto n = std::make_unique<ExprNode>();
      n->kind = ExprKind::kSubquery;
      n->subquery = std::move(sq);
      *out = std::move(n);
      return Status::Ok();
    }
    TokenCursor inner_cur(TokenizeSql(inner));
    std::unique_ptr<ExprNode> inner_expr;
    const Status ps = ParsePredicate(&inner_cur, &inner_expr);
    if (!ps.ok()) return ps;
    *out = std::move(inner_expr);
    return Status::Ok();
  }

  if (IsQuotedLiteral(cur->Peek())) {
    std::string lit;
    cur->Consume(&lit);
    *out = MakeLiteral(UnquoteToken(lit));
    return Status::Ok();
  }
  if (!cur->Peek().empty() &&
      (cur->Peek()[0] == 'x' || cur->Peek()[0] == 'X') && cur->Peek()[1] == '\'') {
    std::string lit;
    cur->Consume(&lit);
    *out = MakeLiteral(lit);
    return Status::Ok();
  }

  std::string ident;
  cur->Consume(&ident);
  if (Upper(ident) == "NULL") {
    *out = MakeLiteral("");
    return Status::Ok();
  }
  if (IsNumericOrRealToken(ident)) {
    *out = MakeLiteral(ident);
    return Status::Ok();
  }
  if (cur->Peek() == ".") {
    cur->Consume(nullptr);
    std::string col;
    cur->Consume(&col);
    *out = MakeColumn(ident, col);
    return Status::Ok();
  }
  if (cur->Peek() == "(") {
    cur->Consume(nullptr);
    auto fn = std::make_unique<ExprNode>();
    fn->kind = ExprKind::kFunction;
    fn->func_name = Upper(ident);
    while (cur->Peek() != ")") {
      std::unique_ptr<ExprNode> arg;
      const Status ps = ParseAdd(cur, &arg);
      if (!ps.ok()) return ps;
      fn->children.push_back(std::move(arg));
      if (cur->Peek() == ",") {
        cur->Consume(nullptr);
        continue;
      }
      if (cur->Peek() == ")") break;
      return Status::InvalidArgument("expected , or ) in function call");
    }
    if (cur->Peek() == ")") cur->Consume(nullptr);
    *out = std::move(fn);
    return Status::Ok();
  }
  *out = MakeColumn("", ident);
  return Status::Ok();
}

Status ExprParse::ParseInOrExists(TokenCursor* cur, std::unique_ptr<ExprNode> lhs,
                                  bool not_op, std::unique_ptr<ExprNode>* out) {
  SubquerySpec sq{};
  sq.not_op = not_op;
  sq.lhs = std::move(lhs);
  if (cur->Peek() == "(") {
    const std::string inner = ExtractBalancedParenSql(cur);
    if (StartsWithSelectKeyword(inner)) {
      sq.sql = inner;
      SelectQuery parsed{};
      TokenCursor ic(TokenizeSql(inner));
      SelectParse sp;
      const Status ps = sp.ParseSelect(inner, &ic, &parsed);
      if (!ps.ok()) return ps;
      sq.parsed_query = std::make_unique<SelectQuery>(std::move(parsed));
      auto n = std::make_unique<ExprNode>();
      n->kind = ExprKind::kSubquery;
      n->subquery = std::move(sq);
      *out = std::move(n);
      return Status::Ok();
    }
    TokenCursor lc(TokenizeSql(inner));
    while (!lc.AtEnd()) {
      std::string lit;
      lc.Consume(&lit);
      if (Upper(lit) == "NULL") {
        sq.in_literals.push_back("");
      } else {
        sq.in_literals.push_back(UnquoteToken(lit));
      }
      if (lc.Peek() == ",") lc.Consume(nullptr);
    }
    auto n = std::make_unique<ExprNode>();
    n->kind = ExprKind::kSubquery;
    n->subquery = std::move(sq);
    *out = std::move(n);
    return Status::Ok();
  }
  if (!cur->AtEnd() && !IsClauseKeyword(cur->PeekUpper())) {
    std::string table;
    cur->Consume(&table);
    sq.sql = "SELECT * FROM " + table;
    SelectQuery parsed{};
    TokenCursor ic(TokenizeSql(sq.sql));
    SelectParse sp;
    const Status ps = sp.ParseSelect(sq.sql, &ic, &parsed);
    if (!ps.ok()) return ps;
    sq.parsed_query = std::make_unique<SelectQuery>(std::move(parsed));
    auto n = std::make_unique<ExprNode>();
    n->kind = ExprKind::kSubquery;
    n->subquery = std::move(sq);
    *out = std::move(n);
    return Status::Ok();
  }
  return Status::InvalidArgument("IN requires parentheses or table");
}

Status ExprParse::ParseMul(TokenCursor* cur, std::unique_ptr<ExprNode>* out) {
  const Status ps = ParseUnary(cur, out);
  if (!ps.ok()) return ps;
  while (cur->Peek() == "*" || cur->Peek() == "/") {
    const std::string op = cur->Peek();
    cur->Consume(nullptr);
    std::unique_ptr<ExprNode> right;
    const Status rs = ParseUnary(cur, &right);
    if (!rs.ok()) return rs;
    BinaryOp bin = op == "*" ? BinaryOp::kMul : BinaryOp::kDiv;
    *out = MakeBinary(bin, std::move(*out), std::move(right));
  }
  return Status::Ok();
}

Status ExprParse::ParseAdd(TokenCursor* cur, std::unique_ptr<ExprNode>* out) {
  const Status ps = ParseMul(cur, out);
  if (!ps.ok()) return ps;
  while (cur->Peek() == "+" || cur->Peek() == "-") {
    const std::string op = cur->Peek();
    cur->Consume(nullptr);
    std::unique_ptr<ExprNode> right;
    const Status rs = ParseMul(cur, &right);
    if (!rs.ok()) return rs;
    BinaryOp bin = op == "+" ? BinaryOp::kAdd : BinaryOp::kSub;
    *out = MakeBinary(bin, std::move(*out), std::move(right));
  }
  return Status::Ok();
}

Status ExprParse::ParseUnary(TokenCursor* cur, std::unique_ptr<ExprNode>* out) {
  if (cur->Peek() == "-") {
    cur->Consume(nullptr);
    std::unique_ptr<ExprNode> inner;
    const Status ps = ParseUnary(cur, &inner);
    if (!ps.ok()) return ps;
    auto zero = std::make_unique<ExprNode>();
    zero->kind = ExprKind::kLiteral;
    zero->literal = "0";
    *out = MakeBinary(BinaryOp::kSub, std::move(zero), std::move(inner));
    return Status::Ok();
  }
  return ParseComparison(cur, out);
}

Status ExprParse::ParseComparison(TokenCursor* cur, std::unique_ptr<ExprNode>* out) {
  std::unique_ptr<ExprNode> left;
  const Status pl = ParsePrimary(cur, &left);
  if (!pl.ok()) return pl;

  if (cur->PeekUpper() == "IS") {
    cur->Consume(nullptr);
    const bool not_null = cur->PeekUpper() == "NOT";
    if (not_null) cur->Consume(nullptr);
    if (cur->PeekUpper() != "NULL") {
      return Status::InvalidArgument("expected NULL");
    }
    cur->Consume(nullptr);
    auto n = std::make_unique<ExprNode>();
    n->kind = ExprKind::kIsNull;
    n->is_null_check = !not_null;
    n->children.push_back(std::move(left));
    *out = std::move(n);
    return Status::Ok();
  }

  if (cur->PeekUpper() == "NOT") {
    if (cur->PeekUpper(1) == "IN") {
      cur->Consume(nullptr);
      cur->Consume(nullptr);
      return ParseInOrExists(cur, std::move(left), true, out);
    }
    if (cur->PeekUpper(1) == "NULL") {
      cur->Consume(nullptr);
      cur->Consume(nullptr);
      auto n = std::make_unique<ExprNode>();
      n->kind = ExprKind::kIsNull;
      n->is_null_check = false;
      n->children.push_back(std::move(left));
      *out = std::move(n);
      return Status::Ok();
    }
  }

  if (cur->PeekUpper() == "IN") {
    cur->Consume(nullptr);
    return ParseInOrExists(cur, std::move(left), false, out);
  }

  if (cur->PeekUpper() == "BETWEEN") {
    cur->Consume(nullptr);
    std::unique_ptr<ExprNode> lo;
    const Status pl = ParsePrimary(cur, &lo);
    if (!pl.ok()) return pl;
    if (cur->PeekUpper() != "AND") {
      return Status::InvalidArgument("BETWEEN missing AND");
    }
    cur->Consume(nullptr);
    std::unique_ptr<ExprNode> hi;
    const Status ph = ParsePrimary(cur, &hi);
    if (!ph.ok()) return ph;
    *out = MakeBinary(
        BinaryOp::kAnd,
        MakeBinary(BinaryOp::kGe, CloneExpr(*left), std::move(lo)),
        MakeBinary(BinaryOp::kLe, CloneExpr(*left), std::move(hi)));
    return Status::Ok();
  }

  if (cur->AtEnd() || IsClauseKeyword(cur->PeekUpper())) {
    *out = std::move(left);
    return Status::Ok();
  }

  const std::string op = cur->Peek();
  BinaryOp bin = BinaryOp::kEq;
  if (op == "=") bin = BinaryOp::kEq;
  else if (op == "!=" || op == "<>") bin = BinaryOp::kNe;
  else if (op == "<") bin = BinaryOp::kLt;
  else if (op == "<=") bin = BinaryOp::kLe;
  else if (op == ">") bin = BinaryOp::kGt;
  else if (op == ">=") bin = BinaryOp::kGe;
  else {
    *out = std::move(left);
    return Status::Ok();
  }
  cur->Consume(nullptr);
  std::unique_ptr<ExprNode> right;
  const Status pr = ParsePrimary(cur, &right);
  if (!pr.ok()) return pr;
  *out = MakeBinary(bin, std::move(left), std::move(right));
  return Status::Ok();
}

Status ExprParse::ParseAnd(TokenCursor* cur, std::unique_ptr<ExprNode>* out) {
  if (!cur || !out) return Status::InvalidArgument("null argument");
  const Status ps = ParseComparison(cur, out);
  if (!ps.ok()) return ps;
  while (cur->PeekUpper() == "AND") {
    cur->Consume(nullptr);
    std::unique_ptr<ExprNode> right;
    const Status rs = ParseComparison(cur, &right);
    if (!rs.ok()) return rs;
    *out = MakeBinary(BinaryOp::kAnd, std::move(*out), std::move(right));
  }
  return Status::Ok();
}

Status ExprParse::ParseOr(TokenCursor* cur, std::unique_ptr<ExprNode>* out) {
  if (!cur || !out) return Status::InvalidArgument("null argument");
  const Status ps = ParseAnd(cur, out);
  if (!ps.ok()) return ps;
  while (cur->PeekUpper() == "OR") {
    cur->Consume(nullptr);
    std::unique_ptr<ExprNode> right;
    const Status rs = ParseAnd(cur, &right);
    if (!rs.ok()) return rs;
    *out = MakeBinary(BinaryOp::kOr, std::move(*out), std::move(right));
  }
  return Status::Ok();
}

Status ExprParse::ParsePredicate(TokenCursor* cur, std::unique_ptr<ExprNode>* out) {
  return ParseOr(cur, out);
}

Status ExprParse::ParseExpr(TokenCursor* cur, std::unique_ptr<ExprNode>* out) {
  return ParseAdd(cur, out);
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
