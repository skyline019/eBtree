#include "select_parse.h"

#include <cctype>
#include <cstdlib>

#include "sql/parse/core/lex_pipeline.h"
#include "sql/parse/native/expr_parse.h"
#include "sql/parse/shared/parse_shared.h"

namespace ebtree {
namespace sql {
namespace parse {

namespace {

bool IsJoinLead(const std::string& u) {
  return u == "JOIN" || u == "LEFT" || u == "RIGHT" || u == "INNER" ||
         u == "CROSS" || u == "FULL";
}

JoinType ParseJoinType(TokenCursor* cur) {
  JoinType t = JoinType::kInner;
  if (cur->PeekUpper() == "CROSS") {
    cur->Consume(nullptr);
    if (cur->PeekUpper() == "JOIN") cur->Consume(nullptr);
    return JoinType::kCross;
  }
  if (cur->PeekUpper() == "LEFT") {
    cur->Consume(nullptr);
    t = JoinType::kLeft;
    if (cur->PeekUpper() == "OUTER") cur->Consume(nullptr);
  } else if (cur->PeekUpper() == "RIGHT") {
    cur->Consume(nullptr);
    t = JoinType::kRight;
    if (cur->PeekUpper() == "OUTER") cur->Consume(nullptr);
  } else if (cur->PeekUpper() == "FULL") {
    cur->Consume(nullptr);
    t = JoinType::kFull;
    if (cur->PeekUpper() == "OUTER") cur->Consume(nullptr);
  } else if (cur->PeekUpper() == "INNER") {
    cur->Consume(nullptr);
  }
  if (cur->PeekUpper() == "JOIN") cur->Consume(nullptr);
  return t;
}

Status ParseJoinOn(TokenCursor* cur, const std::string& left_table,
                   JoinSpec* j) {
  if (cur->PeekUpper() != "ON") {
    return Status::InvalidArgument("JOIN missing ON");
  }
  cur->Consume(nullptr);
  std::string left;
  cur->Consume(&left);
  if (cur->Peek() == ".") {
    cur->Consume(nullptr);
    std::string lc;
    cur->Consume(&lc);
    const auto dot = left.find('.');
    if (dot == std::string::npos) {
      j->left_table = left;
      j->left_col = lc;
    } else {
      j->left_table = left.substr(0, dot);
      j->left_col = left.substr(dot + 1);
    }
  } else {
    j->left_col = left;
    j->left_table = left_table;
  }
  std::string eq;
  cur->Consume(&eq);
  (void)eq;
  std::string right;
  cur->Consume(&right);
  if (cur->Peek() == ".") {
    cur->Consume(nullptr);
    std::string rc;
    cur->Consume(&rc);
    j->right_table = right;
    j->right_col = rc;
  } else {
    j->right_col = right;
    j->right_table = j->table;
  }
  return Status::Ok();
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

bool IsAggregateFunc(const std::string& u) {
  return u == "COUNT" || u == "SUM" || u == "MIN" || u == "MAX" || u == "AVG" ||
         u == "TOTAL" || u == "GROUP_CONCAT";
}

bool IsNumericOrRealToken(const std::string& tok) {
  if (IsNumericToken(tok)) return true;
  if (tok.empty()) return false;
  char* end = nullptr;
  std::strtod(tok.c_str(), &end);
  return end != tok.c_str() && *end == '\0';
}

std::unique_ptr<ExprNode> MakeLiteralNode(const std::string& v) {
  auto n = std::make_unique<ExprNode>();
  n->kind = ExprKind::kLiteral;
  n->literal = v;
  return n;
}

bool IsProjectionClauseEnd(const TokenCursor* cur) {
  if (cur->AtEnd() || cur->Peek() == ",") return true;
  const std::string u = cur->PeekUpper();
  return u == "FROM" || u == "WHERE" || u == "GROUP" || u == "ORDER" ||
         u == "LIMIT" || u == "HAVING";
}

Status ParseProjectionItem(TokenCursor* cur, SelectQuery* out) {
  if (cur->Peek() == "*") {
    cur->Consume(nullptr);
    out->project_cols.push_back("*");
    return Status::Ok();
  }

  if (IsAggregateFunc(cur->PeekUpper())) {
    AggregateSpec agg{};
    agg.func = cur->PeekUpper();
    cur->Consume(nullptr);
    if (cur->Peek() != "(") {
      return Status::InvalidArgument("aggregate missing (");
    }
    cur->Consume(nullptr);
    if (cur->PeekUpper() == "DISTINCT") {
      agg.distinct = true;
      cur->Consume(nullptr);
    }
    if (cur->Peek() == "*") {
      agg.column = "*";
      cur->Consume(nullptr);
    } else {
      cur->Consume(&agg.column);
      if (cur->Peek() == ".") {
        cur->Consume(nullptr);
        std::string part;
        cur->Consume(&part);
        agg.column = agg.column + "." + part;
      }
    }
    if (agg.func == "GROUP_CONCAT" && cur->Peek() == ",") {
      cur->Consume(nullptr);
      std::string sep_lit;
      cur->Consume(&sep_lit);
      agg.separator = UnquoteToken(sep_lit);
    }
    if (cur->Peek() == ")") cur->Consume(nullptr);
    agg.alias = agg.func + "_" + agg.column;
    if (cur->PeekUpper() == "AS") {
      cur->Consume(nullptr);
      cur->Consume(&agg.alias);
    }
    out->aggregates.push_back(agg);
    out->project_cols.push_back(agg.alias);
    return Status::Ok();
  }

  const size_t mark = cur->pos();
  ExprParse ep;
  std::unique_ptr<ExprNode> expr;
  const Status es = ep.ParseExpr(cur, &expr);
  if (es.ok() && expr) {
    if (expr->kind == ExprKind::kColumn && IsProjectionClauseEnd(cur)) {
      const std::string col =
          expr->table.empty() ? expr->column : expr->table + "." + expr->column;
      if (cur->PeekUpper() == "AS") {
        cur->Consume(nullptr);
        std::string alias;
        cur->Consume(&alias);
        out->project_cols.push_back(alias);
      } else {
        out->project_cols.push_back(col);
      }
      return Status::Ok();
    }
    out->scalar_projects.push_back(std::move(expr));
    std::string alias = "expr";
    if (out->scalar_projects.back()->kind == ExprKind::kLiteral) {
      alias = out->scalar_projects.back()->literal;
    }
    if (cur->PeekUpper() == "AS") {
      cur->Consume(nullptr);
      cur->Consume(&alias);
    }
    out->project_cols.push_back(alias);
    return Status::Ok();
  }
  cur->SetPos(mark);

  if (IsQuotedLiteral(cur->Peek())) {
    std::string lit;
    cur->Consume(&lit);
    const std::string val = UnquoteToken(lit);
    out->scalar_projects.push_back(MakeLiteralNode(val));
    if (cur->PeekUpper() == "AS") {
      cur->Consume(nullptr);
      std::string alias;
      cur->Consume(&alias);
      out->project_cols.push_back(alias);
    } else {
      out->project_cols.push_back(val);
    }
    return Status::Ok();
  }

  std::string col;
  cur->Consume(&col);
  if (cur->Peek() == ".") {
    cur->Consume(nullptr);
    std::string part;
    cur->Consume(&part);
    col = col + "." + part;
  }
  if (IsNumericOrRealToken(col)) {
    out->scalar_projects.push_back(MakeLiteralNode(col));
    if (cur->PeekUpper() == "AS") {
      cur->Consume(nullptr);
      std::string alias;
      cur->Consume(&alias);
      out->project_cols.push_back(alias);
    } else {
      out->project_cols.push_back(col);
    }
    return Status::Ok();
  }
  if (cur->PeekUpper() == "AS") {
    cur->Consume(nullptr);
    std::string alias;
    cur->Consume(&alias);
    out->project_cols.push_back(alias);
  } else {
    out->project_cols.push_back(col);
  }
  return Status::Ok();
}

}  // namespace

Status SelectParse::ParseSelect(const std::string& raw_sql, TokenCursor* cur,
                                SelectQuery* out) {
  if (!cur || !out) return Status::InvalidArgument("null argument");
  if (cur->PeekUpper() != "SELECT") {
    return Status::InvalidArgument("not a SELECT");
  }
  cur->Consume(nullptr);

  if (cur->PeekUpper() == "DISTINCT") {
    out->distinct = true;
    cur->Consume(nullptr);
  }

  while (!cur->AtEnd() && cur->PeekUpper() != "FROM") {
    const Status pi = ParseProjectionItem(cur, out);
    if (!pi.ok()) return pi;
    if (cur->Peek() == ",") cur->Consume(nullptr);
  }
  if (out->project_cols.empty() && out->scalar_projects.empty() &&
      out->aggregates.empty()) {
    return Status::InvalidArgument("SELECT missing projection");
  }
  if (cur->PeekUpper() != "FROM") {
    if (out->joins.empty()) {
      out->max_pages = ExtractMaxPagesHint(raw_sql);
      return Status::Ok();
    }
    return Status::InvalidArgument("SELECT missing FROM");
  }
  cur->Consume(nullptr);
  cur->Consume(&out->from_table);

  std::string base_table = out->from_table;
  while (IsJoinLead(cur->PeekUpper())) {
    JoinSpec j{};
    j.type = ParseJoinType(cur);
    cur->Consume(&j.table);
    j.left_table = base_table;
    if (j.type != JoinType::kCross) {
      const Status js = ParseJoinOn(cur, base_table, &j);
      if (!js.ok()) return js;
    }
    out->joins.push_back(j);
    base_table = j.table;
  }

  if (cur->PeekUpper() == "WHERE") {
    cur->Consume(nullptr);
    ExprParse ep;
    std::unique_ptr<ExprNode> where;
    const Status ws = ep.ParsePredicate(cur, &where);
    if (!ws.ok()) return ws;
    out->where = std::move(where);
  }

  if (cur->PeekUpper() == "GROUP") {
    cur->Consume(nullptr);
    if (cur->PeekUpper() != "BY") {
      return Status::InvalidArgument("GROUP missing BY");
    }
    cur->Consume(nullptr);
    while (!cur->AtEnd() && cur->PeekUpper() != "HAVING" &&
           cur->PeekUpper() != "ORDER" && cur->PeekUpper() != "LIMIT") {
      std::string gb;
      cur->Consume(&gb);
      if (cur->Peek() == ".") {
        cur->Consume(nullptr);
        std::string part;
        cur->Consume(&part);
        gb = gb + "." + part;
      }
      out->group_by.push_back(gb);
      if (cur->Peek() == ",") cur->Consume(nullptr);
    }
  }

  if (cur->PeekUpper() == "HAVING") {
    cur->Consume(nullptr);
    ExprParse ep;
    std::unique_ptr<ExprNode> having;
    const Status hs = ep.ParsePredicate(cur, &having);
    if (!hs.ok()) return hs;
    out->having = std::move(having);
  }

  if (cur->PeekUpper() == "ORDER") {
    cur->Consume(nullptr);
    if (cur->PeekUpper() != "BY") {
      return Status::InvalidArgument("ORDER missing BY");
    }
    cur->Consume(nullptr);
    OrderSpec o{};
    cur->Consume(&o.column);
    if (cur->PeekUpper() == "DESC") {
      o.descending = true;
      cur->Consume(nullptr);
    } else if (cur->PeekUpper() == "ASC") {
      cur->Consume(nullptr);
    }
    out->order_by.push_back(o);
  }

  if (cur->PeekUpper() == "LIMIT") {
    cur->Consume(nullptr);
    std::string lim;
    cur->Consume(&lim);
    try {
      out->limit = static_cast<uint64_t>(std::stoull(lim));
    } catch (...) {
      return Status::InvalidArgument("invalid LIMIT");
    }
  }

  out->max_pages = ExtractMaxPagesHint(raw_sql);
  return Status::Ok();
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
