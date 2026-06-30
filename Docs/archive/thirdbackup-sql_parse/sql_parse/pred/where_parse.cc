#include "sql_parse/pred/where_parse.h"

#include "common/parse_error.h"

#include "sql_parse/expr/expr_parse.h"
#include "sql_parse/pred/where_lower.h"
#include "sql_parse/shared/parse_shared.h"
#include "sql_parse/stmt/dml/dml_api.h"
#include "sql_parse/stmt/parse_common_api.h"
#include "sql_parse/stmt/where/where_api.h"

namespace heterodb::sql_parse::pred {

Status ParseWhere(const std::vector<std::string>& tokens, size_t* pos,
                  SqlStatement* out) {
  if (pos == nullptr || out == nullptr) {
    return Status::InvalidArgument("null argument");
  }
  const size_t save = *pos;
  Status s = detail::ParseWhereClause(tokens, pos, out);
  if (s.ok() && *pos < tokens.size() && !detail::IsWhereStopToken(tokens[*pos])) {
    out->where_and.clear();
    out->where_or.clear();
    out->where_expr.reset();
    out->where.reset();
    out->key_range.reset();
    *pos = save;
    s = Status::Syntax("where clause incomplete", ParseErrorKind::kSyntax);
  }
  if (!s.ok()) {
    *pos = save;
    size_t expr_pos = *pos;
    if (expr_pos < tokens.size() && Upper(tokens[expr_pos]) == "WHERE") {
      ++expr_pos;
    }
    Expr* wexpr = nullptr;
    if (::heterodb::sql_parse::ParseExpr(tokens, &expr_pos, &wexpr).ok() &&
        wexpr != nullptr &&
        (expr_pos >= tokens.size() ||
         detail::IsWhereStopToken(tokens[expr_pos]))) {
      *pos = expr_pos;
      out->where_expr.reset(wexpr);
      detail::SyncKeyScanRange(out);
      ApplyWhereCanonicalize(out);
      return Status::OK();
    }
    delete wexpr;
    *pos = save;
    s = detail::ParseSimpleWhere(tokens, pos, out);
    if (!s.ok()) {
      return s;
    }
  }
  ApplyWhereCanonicalize(out);
  return Status::OK();
}

Status ParseWhere(ParseContext* ctx) {
  if (ctx == nullptr || ctx->out == nullptr) {
    return Status::InvalidArgument("null context");
  }
  if (ctx->cursor.AtEnd() || ctx->HeadUpper() != "WHERE") {
    return Status::Syntax("expected WHERE", ParseErrorKind::kSyntax);
  }
  size_t pos = ctx->cursor.pos();
  Status s = ParseWhere(ctx->cursor.tokens(), &pos, ctx->out);
  if (s.ok()) {
    ctx->cursor.set_pos(pos);
  } else {
    ctx->EmitDiagnostic("PredWhereClause", s.message());
  }
  return s;
}

}  // namespace heterodb::sql_parse::pred
