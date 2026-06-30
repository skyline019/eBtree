#include "sql_parse/pred/having_parse.h"

#include "common/parse_error.h"

#include <algorithm>

#include "sql_parse/expr/expr_parse.h"
#include "sql_parse/pred/where_lower.h"
#include "sql_parse/pred/where_normalize.h"
#include "sql_parse/shared/parse_shared.h"
#include "sql_parse/stmt/where/where_api.h"

namespace heterodb::sql_parse::pred {

namespace {

bool HasTopLevelOr(const std::vector<std::string>& tokens) {
  int depth = 0;
  for (const std::string& tok : tokens) {
    if (tok == "(") {
      ++depth;
    } else if (tok == ")") {
      --depth;
    } else if (depth == 0 && Upper(tok) == "OR") {
      return true;
    }
  }
  return false;
}

bool IsHavingBoundary(const std::vector<std::string>& tokens, size_t p) {
  if (p >= tokens.size()) {
    return true;
  }
  const std::string u = Upper(tokens[p]);
  return u == "LIMIT" || u == "ORDER" || u == "UNION";
}

}  // namespace

Status ParseHaving(const std::vector<std::string>& tokens, size_t* pos,
                   SqlStatement* out) {
  if (pos == nullptr || out == nullptr) {
    return Status::InvalidArgument("null argument");
  }
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "HAVING") {
    return Status::Syntax("expected HAVING", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  out->having_and.clear();
  out->having_expr.reset();
  const size_t having_start = *pos;
  size_t having_end = having_start;
  while (!IsHavingBoundary(tokens, having_end)) {
    ++having_end;
  }
  *pos = having_start;
  if (having_end > having_start) {
    std::vector<std::string> slice(
        tokens.begin() + static_cast<std::ptrdiff_t>(having_start),
        tokens.begin() + static_cast<std::ptrdiff_t>(having_end));
    if (HasTopLevelOr(slice)) {
      size_t hpos = 0;
      Expr* hexpr = nullptr;
      if (::heterodb::sql_parse::ParseExpr(slice, &hpos, &hexpr).ok() &&
          hpos == slice.size()) {
        out->having_expr.reset(hexpr);
        *pos = having_end;
        ApplyHavingCanonicalize(out);
        return Status::OK();
      }
      DeleteExpr(hexpr);
      return Status::Syntax("invalid HAVING OR expression", ParseErrorKind::kSyntax);
    }
  }
  KeyScanRange dummy;
  dummy.end_exclusive = "\xFF";
  auto parse_one_having = [&]() -> Status {
    auto try_having_agg = [&](const std::string& name, AggregateFn fn) -> bool {
      if (*pos >= tokens.size() || Upper(tokens[*pos]) != name) {
        return false;
      }
      if (*pos + 1 >= tokens.size() || tokens[*pos + 1] != "(") {
        return false;
      }
      ++(*pos);
      if (*pos >= tokens.size() || tokens[*pos] != "(") {
        return false;
      }
      ++(*pos);
      std::string having_agg_col;
      if (*pos < tokens.size() && tokens[*pos] != ")") {
        having_agg_col = tokens[(*pos)++];
      }
      if (*pos >= tokens.size() || tokens[*pos] != ")") {
        return false;
      }
      ++(*pos);
      if (*pos + 1 >= tokens.size()) {
        return false;
      }
      ColumnPredicate cp;
      cp.column = detail::AggregateOutputColumn(fn);
      Status cs = detail::ParseCompareOp(tokens[(*pos)++], &cp.op);
      if (!cs.ok()) {
        return false;
      }
      cp.value = detail::Unquote(tokens[(*pos)++]);
      out->having_and.push_back(cp);
      if (out->aggregate == AggregateFn::kNone) {
        out->aggregate = fn;
        if (!having_agg_col.empty() && having_agg_col != "*") {
          const size_t dot = having_agg_col.find('.');
          out->aggregate_column =
              dot == std::string::npos ? having_agg_col
                                       : having_agg_col.substr(dot + 1);
        }
      }
      const std::string agg_col = detail::AggregateOutputColumn(fn);
      if (std::find(out->columns.begin(), out->columns.end(), agg_col) ==
          out->columns.end()) {
        out->columns.push_back(agg_col);
        out->select_exprs.push_back(nullptr);
      }
      return true;
    };
    if (try_having_agg("COUNT", AggregateFn::kCount) ||
        try_having_agg("SUM", AggregateFn::kSum) ||
        try_having_agg("AVG", AggregateFn::kAvg) ||
        try_having_agg("MIN", AggregateFn::kMin) ||
        try_having_agg("MAX", AggregateFn::kMax)) {
      return Status::OK();
    }
    const size_t base = out->where_and.size();
    Status hs = detail::ParseColumnPredicate(tokens, pos, &dummy, out);
    if (!hs.ok()) {
      return hs;
    }
    out->having_and.insert(
        out->having_and.end(),
        out->where_and.begin() + static_cast<std::ptrdiff_t>(base),
        out->where_and.end());
    out->where_and.resize(base);
    return Status::OK();
  };
  Status hs = parse_one_having();
  if (hs.ok()) {
    while (*pos < tokens.size() && Upper(tokens[*pos]) == "AND") {
      ++(*pos);
      hs = parse_one_having();
      if (!hs.ok()) {
        return hs;
      }
    }
  } else {
    *pos = having_start;
    out->having_and.clear();
  }
  if (out->having_and.empty() && !out->having_expr && having_end > having_start) {
    std::vector<std::string> slice(
        tokens.begin() + static_cast<std::ptrdiff_t>(having_start),
        tokens.begin() + static_cast<std::ptrdiff_t>(having_end));
    size_t hpos = 0;
    Expr* hexpr = nullptr;
    if (::heterodb::sql_parse::ParseExpr(slice, &hpos, &hexpr).ok() &&
        hpos == slice.size()) {
      out->having_expr.reset(hexpr);
      *pos = having_end;
    } else {
      DeleteExpr(hexpr);
      return Status::Syntax("invalid HAVING expression", ParseErrorKind::kSyntax);
    }
  }
  if (out->having_and.empty() && !out->having_expr) {
    return Status::Syntax("HAVING requires predicate", ParseErrorKind::kSyntax);
  }
  ApplyHavingCanonicalize(out);
  return Status::OK();
}

Status ParseHaving(ParseContext* ctx) {
  if (ctx == nullptr || ctx->out == nullptr) {
    return Status::InvalidArgument("null context");
  }
  if (ctx->cursor.AtEnd() || ctx->HeadUpper() != "HAVING") {
    return Status::Syntax("expected HAVING", ParseErrorKind::kSyntax);
  }
  size_t pos = ctx->cursor.pos();
  Status s = ParseHaving(ctx->cursor.tokens(), &pos, ctx->out);
  if (s.ok()) {
    ctx->cursor.set_pos(pos);
  } else {
    ctx->EmitDiagnostic("PredHavingClause", s.message());
  }
  return s;
}

}  // namespace heterodb::sql_parse::pred
