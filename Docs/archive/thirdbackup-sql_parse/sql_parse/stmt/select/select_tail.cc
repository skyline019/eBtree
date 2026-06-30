// ParseConcept | SELECT — ORDER BY / LIMIT / locking tail.
#include "sql_parse/stmt/select/select_api.h"

#include "common/parse_error.h"

#include "concept/query/expr.h"
#include "sql_parse/expr/expr_parse.h"
#include "sql_parse/shared/parse_shared.h"

#include <algorithm>
#include <cctype>

namespace heterodb::sql_parse {
namespace detail {
namespace {

std::string OrderByLabelFromExpr(const Expr* expr) {
  if (expr == nullptr) {
    return {};
  }
  if (expr->kind == ExprKind::kColumn) {
    if (!expr->table_qual.empty()) {
      return expr->table_qual + "." + expr->column;
    }
    return expr->column;
  }
  if (expr->kind == ExprKind::kFunc) {
    std::ostringstream oss;
    oss << expr->func_name << " ( ";
    for (size_t i = 0; i < expr->func_args.size(); ++i) {
      if (i > 0) {
        oss << " , ";
      }
      oss << OrderByLabelFromExpr(expr->func_args[i]);
    }
    oss << " )";
    return oss.str();
  }
  if (expr->kind == ExprKind::kLiteral) {
    return expr->literal.ToSqlString();
  }
  return {};
}

}  // namespace

Status ParseSelectTail(const std::vector<std::string>& tokens, size_t* pos,
                     SqlStatement* out) {
  out->order_by_items.clear();
  out->order_by_column.reset();
  out->order_by_desc = false;
  out->limit.reset();
  out->offset.reset();
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "ORDER") {
    ++(*pos);
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "BY") {
      return Status::Syntax("expected BY after ORDER", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("ORDER BY requires column", ParseErrorKind::kSyntax);
    }
    do {
      if (tokens[*pos] == ",") {
        ++(*pos);
      }
      OrderByItem item;
      size_t expr_pos = *pos;
      Expr* order_expr = nullptr;
      if (::heterodb::sql_parse::ParseExpr(tokens, &expr_pos, &order_expr).ok() && order_expr != nullptr &&
          expr_pos > *pos) {
        if (order_expr->kind == ExprKind::kColumn) {
          item.column = order_expr->column;
          if (!order_expr->table_qual.empty()) {
            item.column = order_expr->table_qual + "." + order_expr->column;
          }
        } else {
          item.column = OrderByLabelFromExpr(order_expr);
        }
        *pos = expr_pos;
      } else {
        const std::string ord_tok = tokens[(*pos)++];
        item.column = ord_tok;
        if (!ord_tok.empty() &&
            std::all_of(ord_tok.begin(), ord_tok.end(),
                        [](unsigned char c) { return std::isdigit(c); })) {
          item.ordinal_position = true;
        }
      }
      if (*pos < tokens.size() && Upper(tokens[*pos]) == "DESC") {
        item.desc = true;
        ++(*pos);
      } else if (*pos < tokens.size() && Upper(tokens[*pos]) == "ASC") {
        ++(*pos);
      }
      if (*pos < tokens.size() && Upper(tokens[*pos]) == "NULLS") {
        ++(*pos);
        if (*pos >= tokens.size()) {
          return Status::Syntax("NULLS requires FIRST or LAST", ParseErrorKind::kSyntax);
        }
        if (Upper(tokens[*pos]) == "FIRST") {
          item.nulls_first = true;
          ++(*pos);
        } else if (Upper(tokens[*pos]) == "LAST") {
          item.nulls_last = true;
          ++(*pos);
        } else {
          return Status::Syntax("NULLS requires FIRST or LAST", ParseErrorKind::kSyntax);
        }
      }
      out->order_by_items.push_back(item);
    } while (*pos < tokens.size() && tokens[*pos] == ",");
    if (!out->order_by_items.empty()) {
      out->order_by_column = out->order_by_items.front().column;
      out->order_by_desc = out->order_by_items.front().desc;
    }
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "LIMIT") {
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("LIMIT requires value", ParseErrorKind::kSyntax);
    }
    out->limit = static_cast<uint64_t>(std::stoull(tokens[(*pos)++]));
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "OFFSET") {
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("OFFSET requires value", ParseErrorKind::kSyntax);
    }
    out->offset = static_cast<uint64_t>(std::stoull(tokens[(*pos)++]));
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "FOR") {
    ++(*pos);
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "UPDATE") {
      return Status::Syntax("FOR UPDATE expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    out->select_for_update = true;
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "NOWAIT") {
      ++(*pos);
      out->select_for_update_nowait = true;
    } else if (*pos + 1 < tokens.size() && Upper(tokens[*pos]) == "SKIP" &&
               Upper(tokens[*pos + 1]) == "LOCKED") {
      *pos += 2;
      out->select_skip_locked = true;
    }
  }
  if (*pos + 2 < tokens.size() && Upper(tokens[*pos]) == "LOCK" &&
      Upper(tokens[*pos + 1]) == "IN" && Upper(tokens[*pos + 2]) == "SHARE") {
    *pos += 3;
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "MODE") {
      ++(*pos);
    }
    out->lock_in_share_mode = true;
  }
  return Status::OK();
}

}  // namespace detail
}  // namespace heterodb::sql_parse
