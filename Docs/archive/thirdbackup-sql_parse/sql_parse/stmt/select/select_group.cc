// ParseConcept | SELECT — GROUP BY / HAVING.
#include "sql_parse/stmt/select/select_api.h"

#include "common/parse_error.h"
#include "sql_parse/stmt/where/where_api.h"
#include "sql_parse/stmt/parse_common_api.h"

#include "concept/query/parser.h"
#include "concept/query/expr.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "concept/catalog/catalog_codec.h"
#include "concept/catalog/qualified_name.h"
#include "concept/catalog/value_codec.h"
#include "concept/schema/schema.h"
#include "sql_parse/expr/expr_parse.h"
#include "sql_parse/pred/having_parse.h"
#include "sql_parse/pred/where_parse.h"
#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {
Status ParseSelectGroupHaving(const std::vector<std::string>& tokens, size_t* pos,
                              SqlStatement* out) {
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "GROUP") {
    ++(*pos);
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "BY") {
      return Status::Syntax("expected BY after GROUP", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("GROUP BY requires column", ParseErrorKind::kSyntax);
    }
    auto is_group_boundary = [&](size_t p) {
      if (p >= tokens.size()) {
        return true;
      }
      const std::string u = Upper(tokens[p]);
      return u == "LIMIT" || u == "ORDER" || u == "GROUP" || u == "HAVING" ||
             u == "UNION" || u == "FOR" || u == "LOCK";
    };
    while (!is_group_boundary(*pos)) {
      if (tokens[*pos] == ",") {
        ++(*pos);
        continue;
      }
      const std::string tok = tokens[*pos];
      const bool is_ord =
          !tok.empty() &&
          std::all_of(tok.begin(), tok.end(),
                      [](unsigned char c) { return std::isdigit(c); });
      if (is_ord) {
        const size_t ord = static_cast<size_t>(std::stoull(tok));
        ++(*pos);
        out->group_by_ordinals.push_back(true);
        out->group_by_exprs.push_back(nullptr);
        if (ord >= 1 && ord <= out->columns.size()) {
          out->group_by_columns.push_back(out->columns[ord - 1]);
        } else {
          out->group_by_columns.push_back(tok);
        }
        continue;
      }
      size_t item_end = *pos + 1;
      while (!is_group_boundary(item_end) && tokens[item_end] != ",") {
        ++item_end;
      }
      std::vector<std::string> slice(tokens.begin() + static_cast<std::ptrdiff_t>(*pos),
                                     tokens.begin() +
                                         static_cast<std::ptrdiff_t>(item_end));
      size_t spos = 0;
      Expr* gexpr = nullptr;
      if (slice.size() == 1) {
        out->group_by_ordinals.push_back(false);
        out->group_by_exprs.push_back(nullptr);
        out->group_by_columns.push_back(slice[0]);
        *pos = item_end;
        continue;
      }
      if (::heterodb::sql_parse::ParseExpr(slice, &spos, &gexpr).ok() && spos == slice.size() &&
          !ExprTreeHasScalarSubquery(*gexpr)) {
        out->group_by_ordinals.push_back(false);
        out->group_by_exprs.push_back(std::shared_ptr<Expr>(gexpr));
        if (gexpr->kind == ExprKind::kColumn) {
          out->group_by_columns.push_back(
              gexpr->table_qual.empty()
                  ? gexpr->column
                  : gexpr->table_qual + "." + gexpr->column);
        } else {
          out->group_by_columns.push_back("group_expr_" +
                                          std::to_string(out->group_by_columns.size()));
        }
        *pos = item_end;
        continue;
      }
      DeleteExpr(gexpr);
      out->group_by_ordinals.push_back(false);
      out->group_by_exprs.push_back(nullptr);
      out->group_by_columns.push_back(tok);
      ++(*pos);
    }
    if (out->group_by_columns.empty()) {
      return Status::Syntax("GROUP BY requires column", ParseErrorKind::kSyntax);
    }
    out->group_by_column = out->group_by_columns.front();
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "HAVING") {
    Status hs = pred::ParseHaving(tokens, pos, out);
    if (!hs.ok()) {
      return hs;
    }
  }
  return Status::OK();
}

}  // namespace detail
}  // namespace heterodb::sql_parse
