// ParseConcept | SELECT — projection list (agg/window/expr).
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
Status ParseSelectProjectList(const std::vector<std::string>& tokens, size_t* pos,
                              SqlStatement* out) {
  auto try_parse_agg_token = [&](const std::string& name,
                                 AggregateFn fn) -> bool {
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != name) {
      return false;
    }
    ++(*pos);
    if (*pos >= tokens.size() || tokens[*pos] != "(") {
      return false;
    }
    ++(*pos);
    if (*pos < tokens.size() && tokens[*pos] != ")") {
      const std::string col = tokens[(*pos)++];
      if (col != "*") {
        const size_t dot = col.find('.');
        out->aggregate_column =
            dot == std::string::npos ? col : col.substr(dot + 1);
      }
    }
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      return false;
    }
    ++(*pos);
    if (out->aggregate != AggregateFn::kNone) {
      return false;
    }
    out->aggregate = fn;
    const std::string col_label = detail::AggregateOutputColumn(fn);
    if (std::find(out->columns.begin(), out->columns.end(), col_label) ==
        out->columns.end()) {
      out->columns.push_back(col_label);
      out->select_exprs.push_back(nullptr);
    }
    return true;
  };
  auto try_parse_window_token = [&](const std::string& name,
                                    WindowFn fn) -> bool {
    const size_t start = *pos;
    size_t p = start;
    if (p >= tokens.size() || Upper(tokens[p]) != name) {
      return false;
    }
    ++p;
    if (p >= tokens.size() || tokens[p] != "(") {
      return false;
    }
    ++p;
    if (p >= tokens.size() || tokens[p] == ")") {
      return false;
    }
    WindowExpr we;
    we.fn = fn;
    we.value_column = tokens[p++];
    if (p < tokens.size() && tokens[p] == ",") {
      ++p;
      if (p < tokens.size()) {
        try {
          we.offset = static_cast<uint32_t>(std::stoul(tokens[p++]));
        } catch (...) {
          return false;
        }
      }
      if (p < tokens.size() && tokens[p] == ",") {
        ++p;
        if (p < tokens.size()) {
          we.default_value = Unquote(tokens[p++]);
        }
      }
    }
    if (p >= tokens.size() || tokens[p] != ")") {
      return false;
    }
    ++p;
    if (p >= tokens.size() || Upper(tokens[p]) != "OVER") {
      return false;
    }
    ++p;
    if (p >= tokens.size()) {
      return false;
    }
    if (tokens[p] == "(") {
      ++p;
      Status ws = ParseWindowOverBody(tokens, &p, &we);
      if (!ws.ok()) {
        return false;
      }
    } else {
      we.named_window_ref = tokens[p++];
    }
    we.output_column = WindowOutputColumn(fn);
    out->window_exprs.push_back(we);
    out->columns.push_back(we.output_column);
    out->select_exprs.push_back(nullptr);
    out->has_window = true;
    *pos = p;
    return true;
  };
  auto try_parse_window_nullary = [&](const std::string& name,
                                      WindowFn fn) -> bool {
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != name) {
      return false;
    }
    ++(*pos);
    if (*pos >= tokens.size() || tokens[*pos] != "(") {
      return false;
    }
    ++(*pos);
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      return false;
    }
    ++(*pos);
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "OVER") {
      return false;
    }
    ++(*pos);
    WindowExpr we;
    we.fn = fn;
    if (*pos >= tokens.size()) {
      return false;
    }
    if (tokens[*pos] == "(") {
      ++(*pos);
      Status ws = ParseWindowOverBody(tokens, pos, &we);
      if (!ws.ok()) {
        return false;
      }
    } else {
      we.named_window_ref = tokens[(*pos)++];
    }
    we.output_column = WindowOutputColumn(fn);
    out->window_exprs.push_back(we);
    out->columns.push_back(we.output_column);
    out->select_exprs.push_back(nullptr);
    out->has_window = true;
    return true;
  };
  while (*pos < tokens.size() && Upper(tokens[*pos]) != "FROM") {
    if (tokens[*pos] == ",") {
      ++(*pos);
      continue;
    }
    if ((*pos < tokens.size() && Upper(tokens[*pos]) == "NTILE")) {
      ++(*pos);
      if (*pos < tokens.size() && tokens[*pos] == "(") {
        ++(*pos);
        if (*pos < tokens.size()) {
          WindowExpr we;
          we.fn = WindowFn::kNtile;
          try {
            we.ntile_buckets = static_cast<uint32_t>(std::stoul(tokens[(*pos)++]));
          } catch (...) {
            return Status::Syntax("NTILE needs bucket count", ParseErrorKind::kSyntax);
          }
          if (*pos < tokens.size() && tokens[*pos] == ")") {
            ++(*pos);
            if (*pos < tokens.size() && Upper(tokens[*pos]) == "OVER") {
              ++(*pos);
              if (*pos < tokens.size() && tokens[*pos] == "(") {
                ++(*pos);
                Status ws = ParseWindowOverBody(tokens, pos, &we);
                if (ws.ok()) {
                  we.output_column = WindowOutputColumn(WindowFn::kNtile);
                  out->window_exprs.push_back(we);
                  out->columns.push_back(we.output_column);
                  out->select_exprs.push_back(nullptr);
                  out->has_window = true;
                  if (*pos < tokens.size() && Upper(tokens[*pos]) == "AS") {
                    ++(*pos);
                    if (*pos < tokens.size()) {
                      out->columns.back() = tokens[(*pos)++];
                      out->window_exprs.back().output_column = out->columns.back();
                    }
                  }
                  continue;
                }
              }
            }
          }
        }
      }
      return Status::Syntax("invalid NTILE syntax", ParseErrorKind::kSyntax);
    }
    if (try_parse_window_nullary("ROW_NUMBER", WindowFn::kRowNumber) ||
        try_parse_window_nullary("RANK", WindowFn::kRank) ||
        try_parse_window_nullary("DENSE_RANK", WindowFn::kDenseRank) ||
        try_parse_window_token("LAG", WindowFn::kLag) ||
        try_parse_window_token("LEAD", WindowFn::kLead) ||
        try_parse_window_token("SUM", WindowFn::kSum) ||
        try_parse_window_token("AVG", WindowFn::kAvg) ||
        try_parse_window_token("FIRST_VALUE", WindowFn::kFirstValue) ||
        try_parse_window_token("LAST_VALUE", WindowFn::kLastValue)) {
      if (*pos < tokens.size() && Upper(tokens[*pos]) == "AS") {
        ++(*pos);
        if (*pos < tokens.size()) {
          out->columns.back() = tokens[(*pos)++];
          out->window_exprs.back().output_column = out->columns.back();
        }
      }
      continue;
    }
    if (try_parse_agg_token("COUNT", AggregateFn::kCount) ||
        try_parse_agg_token("SUM", AggregateFn::kSum) ||
        try_parse_agg_token("AVG", AggregateFn::kAvg) ||
        try_parse_agg_token("MIN", AggregateFn::kMin) ||
        try_parse_agg_token("MAX", AggregateFn::kMax)) {
      continue;
    }
    if (tokens[*pos] == "*") {
      ++(*pos);
      continue;
    }
    const size_t expr_save = *pos;
    Expr* raw = nullptr;
    if (::heterodb::sql_parse::ParseExpr(tokens, pos, &raw).ok()) {
      std::string alias = "expr";
      if (*pos < tokens.size() && Upper(tokens[*pos]) == "AS") {
        ++(*pos);
        if (*pos < tokens.size()) {
          alias = tokens[(*pos)++];
        }
      } else if (raw->kind == ExprKind::kColumn) {
        if (!raw->table_qual.empty() && !raw->column.empty()) {
          alias = raw->table_qual + "." + raw->column;
        } else if (!raw->table_qual.empty()) {
          alias = raw->table_qual;
        } else {
          alias = raw->column;
        }
      }
      out->columns.push_back(alias);
      out->select_exprs.push_back(std::shared_ptr<Expr>(raw));
      continue;
    }
    DeleteExpr(raw);
    *pos = expr_save;
    out->columns.push_back(tokens[(*pos)++]);
    out->select_exprs.push_back(nullptr);
  }
  return Status::OK();
}


}  // namespace detail
}  // namespace heterodb::sql_parse
