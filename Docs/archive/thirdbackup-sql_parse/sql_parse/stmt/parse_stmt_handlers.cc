#include "sql_parse/stmt/parse_stmt_handlers.h"

#include "common/parse_error.h"

#include "sql_parse/core/parse_context.h"
#include "sql_parse/shared/parse_shared.h"
#include "sql_parse/stmt/dispatch/dispatch_api.h"

namespace heterodb::sql_parse {

namespace {

bool HeadIs(const ParseContext& ctx, const char* kw) {
  return ctx.HeadUpper() == kw;
}

bool HeadIsAny(const ParseContext& ctx,
               std::initializer_list<const char*> keywords) {
  const std::string head = ctx.HeadUpper();
  for (const char* kw : keywords) {
    if (head == kw) {
      return true;
    }
  }
  return false;
}

size_t StmtBodyPos(ParseContext* ctx) {
  size_t pos = ctx->cursor.pos();
  if (pos == 0 && !ctx->cursor.AtEnd()) {
    pos = 1;
  }
  return pos;
}

}  // namespace

Status DispatchSelectStatement(ParseContext* ctx) {
  if (ctx == nullptr || ctx->out == nullptr) {
    return Status::InvalidArgument("null parse context");
  }
  const auto& tokens = ctx->cursor.tokens();
  if (tokens.empty()) {
    return Status::Syntax("empty sql", ParseErrorKind::kSyntax);
  }
  if (!HeadIsAny(*ctx, {"SELECT", "WITH"})) {
    return Status::Syntax("expected SELECT or WITH", ParseErrorKind::kSyntax);
  }
  size_t pos = StmtBodyPos(ctx);
  Status s = detail::ParseStmtQuery(tokens, &pos, ctx->out);
  if (s.ok()) {
    ctx->cursor.set_pos(pos);
  }
  return s;
}

Status DispatchDmlStatement(ParseContext* ctx) {
  if (ctx == nullptr || ctx->out == nullptr) {
    return Status::InvalidArgument("null parse context");
  }
  if (!HeadIsAny(*ctx, {"INSERT", "UPDATE", "DELETE", "REPLACE", "TRUNCATE"})) {
    return Status::Syntax("expected DML statement", ParseErrorKind::kSyntax);
  }
  size_t pos = StmtBodyPos(ctx);
  Status s =
      detail::ParseStmtDml(ctx->HeadUpper(), ctx->cursor.tokens(), &pos, ctx->out);
  if (s.ok()) {
    ctx->cursor.set_pos(pos);
  }
  return s;
}

Status DispatchDdlStatement(ParseContext* ctx) {
  if (ctx == nullptr || ctx->out == nullptr) {
    return Status::InvalidArgument("null parse context");
  }
  if (!HeadIsAny(*ctx, {"CREATE", "DROP", "ALTER", "REINDEX"})) {
    return Status::Syntax("expected DDL statement", ParseErrorKind::kSyntax);
  }
  size_t pos = StmtBodyPos(ctx);
  Status s =
      detail::ParseStmtDdl(ctx->HeadUpper(), ctx->cursor.tokens(), &pos, ctx->out);
  if (s.ok()) {
    ctx->cursor.set_pos(pos);
  }
  return s;
}

Status DispatchTxnStatement(ParseContext* ctx) {
  if (ctx == nullptr || ctx->out == nullptr) {
    return Status::InvalidArgument("null parse context");
  }
  if (!HeadIsAny(*ctx, {"BEGIN", "START", "COMMIT", "ROLLBACK", "SAVEPOINT",
                        "RELEASE"})) {
    return Status::Syntax("expected transaction statement", ParseErrorKind::kSyntax);
  }
  size_t pos = StmtBodyPos(ctx);
  Status s =
      detail::ParseStmtTxn(ctx->HeadUpper(), ctx->cursor.tokens(), &pos, ctx->out);
  if (s.ok()) {
    ctx->cursor.set_pos(pos);
  }
  return s;
}

Status DispatchMetaStatement(ParseContext* ctx) {
  if (ctx == nullptr || ctx->out == nullptr) {
    return Status::InvalidArgument("null parse context");
  }
  if (!HeadIsAny(*ctx, {"USE", "SHOW", "DESCRIBE", "DESC", "EXPLAIN"})) {
    return Status::Syntax("expected meta statement", ParseErrorKind::kSyntax);
  }
  size_t pos = StmtBodyPos(ctx);
  Status s =
      detail::ParseStmtMeta(ctx->HeadUpper(), ctx->cursor.tokens(), &pos, ctx->out);
  if (s.ok()) {
    ctx->cursor.set_pos(pos);
  }
  return s;
}

Status DispatchPrivStatement(ParseContext* ctx) {
  if (ctx == nullptr || ctx->out == nullptr) {
    return Status::InvalidArgument("null parse context");
  }
  if (!HeadIsAny(*ctx, {"GRANT", "REVOKE"})) {
    return Status::Syntax("expected GRANT or REVOKE", ParseErrorKind::kSyntax);
  }
  size_t pos = StmtBodyPos(ctx);
  Status s =
      detail::ParseStmtPriv(ctx->HeadUpper(), ctx->cursor.tokens(), &pos, ctx->out);
  if (s.ok()) {
    ctx->cursor.set_pos(pos);
  }
  return s;
}

Status DispatchSetStatement(ParseContext* ctx) {
  if (ctx == nullptr || ctx->out == nullptr) {
    return Status::InvalidArgument("null parse context");
  }
  if (!HeadIs(*ctx, "SET")) {
    return Status::Syntax("expected SET", ParseErrorKind::kSyntax);
  }
  size_t pos = StmtBodyPos(ctx);
  Status s = detail::ParseStmtSet(ctx->cursor.tokens(), &pos, ctx->out);
  if (s.ok()) {
    ctx->cursor.set_pos(pos);
  }
  return s;
}

}  // namespace heterodb::sql_parse
