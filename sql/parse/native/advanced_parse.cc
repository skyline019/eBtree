#include "advanced_parse.h"

#include "sql/parse/core/lex_pipeline.h"
#include "sql/parse/core/token_cursor.h"
#include "sql/parse/native/select_parse.h"
#include "sql/parse/shared/parse_shared.h"

namespace ebtree {
namespace sql {
namespace parse {

namespace {

std::string ExtractBalancedFromCursor(TokenCursor* cur) {
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

SetOpKind ParseSetOpKindWord(const std::string& u) {
  if (u == "UNION") return SetOpKind::kUnion;
  if (u == "INTERSECT") return SetOpKind::kIntersect;
  return SetOpKind::kExcept;
}

}  // namespace

Status ParseCteQuery(const std::string& raw_sql, QueryStatement* out) {
  if (!out) return Status::InvalidArgument("out is null");
  TokenCursor cur(TokenizeSql(raw_sql));
  if (cur.PeekUpper() != "WITH") {
    return Status::InvalidArgument("not a CTE query");
  }
  cur.Consume(nullptr);
  CteQuery cq{};
  if (cur.PeekUpper() == "RECURSIVE") {
    cq.recursive = true;
    cur.Consume(nullptr);
  }
  do {
    CteEntry entry{};
    cur.Consume(&entry.name);
    if (cur.PeekUpper() != "AS") {
      return Status::InvalidArgument("CTE missing AS");
    }
    cur.Consume(nullptr);
    const std::string inner = ExtractBalancedFromCursor(&cur);
    if (inner.empty()) return Status::InvalidArgument("CTE missing body");
    entry.query = std::make_unique<SelectQuery>();
    TokenCursor ic(TokenizeSql(inner));
    SelectParse sp;
    const Status ps = sp.ParseSelect(inner, &ic, entry.query.get());
    if (!ps.ok()) return ps;
    cq.ctes.push_back(std::move(entry));
  } while (cur.Peek() == "," && (cur.Consume(nullptr), true));

  cq.main_query = std::make_unique<SelectQuery>();
  SelectParse sp;
  const Status ms = sp.ParseSelect(raw_sql, &cur, cq.main_query.get());
  if (!ms.ok()) return ms;
  out->kind = QueryStmtKind::kWithCte;
  out->cte_query = std::move(cq);
  return Status::Ok();
}

Status ParseSetOpQuery(const std::string& raw_sql, QueryStatement* out) {
  if (!out) return Status::InvalidArgument("out is null");
  const std::string upper = Upper(raw_sql);
  size_t pos = upper.find(" UNION ");
  SetOpKind op = SetOpKind::kUnion;
  if (pos == std::string::npos) {
    pos = upper.find(" INTERSECT ");
    op = SetOpKind::kIntersect;
  }
  if (pos == std::string::npos) {
    pos = upper.find(" EXCEPT ");
    op = SetOpKind::kExcept;
  }
  if (pos == std::string::npos) {
    return Status::InvalidArgument("not a set op query");
  }

  const std::string left_sql = Trim(raw_sql.substr(0, pos));
  std::string right_part = Trim(raw_sql.substr(pos + 1));
  const size_t sp = right_part.find(' ');
  if (sp == std::string::npos) {
    return Status::InvalidArgument("invalid set op");
  }
  const std::string op_word = Upper(right_part.substr(0, sp));
  right_part = Trim(right_part.substr(sp + 1));
  bool all = false;
  if (Upper(right_part).find("ALL ") == 0) {
    all = true;
    right_part = Trim(right_part.substr(4));
  }

  SetOpQuery so{};
  so.op = ParseSetOpKindWord(op_word);
  so.all = all;
  so.left = std::make_unique<SelectQuery>();
  so.right = std::make_unique<SelectQuery>();
  TokenCursor lc(TokenizeSql(left_sql));
  TokenCursor rc(TokenizeSql(right_part));
  SelectParse sel;
  const Status ls = sel.ParseSelect(left_sql, &lc, so.left.get());
  if (!ls.ok()) return ls;
  const Status rs = sel.ParseSelect(right_part, &rc, so.right.get());
  if (!rs.ok()) return rs;
  out->kind = QueryStmtKind::kSetOp;
  out->setop_query = std::move(so);
  return Status::Ok();
}

Status ParseWindowQuery(const std::string& raw_sql, QueryStatement* out) {
  if (!out) return Status::InvalidArgument("out is null");
  TokenCursor cur(TokenizeSql(raw_sql));
  if (cur.PeekUpper() != "SELECT") {
    return Status::InvalidArgument("not a window select");
  }
  cur.Consume(nullptr);

  WindowQuery wq{};
  cur.Consume(&wq.window_func);
  if (cur.Peek() == "(") {
    cur.Consume(nullptr);
    if (cur.Peek() == "*") cur.Consume(nullptr);
    if (cur.Peek() == ")") cur.Consume(nullptr);
  }

  while (!cur.AtEnd() && cur.PeekUpper() != "FROM") {
    if (cur.PeekUpper() == "OVER") {
      cur.Consume(nullptr);
      if (cur.Peek() == "(") cur.Consume(nullptr);
      if (cur.PeekUpper() == "PARTITION") {
        cur.Consume(nullptr);
        if (cur.PeekUpper() == "BY") cur.Consume(nullptr);
        cur.Consume(&wq.partition_col);
      }
      if (cur.PeekUpper() == "ORDER") {
        cur.Consume(nullptr);
        if (cur.PeekUpper() == "BY") cur.Consume(nullptr);
        cur.Consume(&wq.order_col);
        if (cur.PeekUpper() == "DESC") {
          wq.order_desc = true;
          cur.Consume(nullptr);
        }
      }
      if (cur.Peek() == ")") cur.Consume(nullptr);
      break;
    }
    cur.Consume(nullptr);
  }

  wq.query = std::make_unique<SelectQuery>();
  if (cur.PeekUpper() == "FROM") {
    cur.Consume(nullptr);
    cur.Consume(&wq.query->from_table);
  }
  wq.query->project_cols.push_back(wq.window_func);
  out->kind = QueryStmtKind::kWindowSelect;
  out->window_query = std::move(wq);
  return Status::Ok();
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
