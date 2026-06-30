#include "sql_parse/bootstrap/parse_module.h"
#include "sql_parse/core/parse_context.h"
#include "sql_parse/core/parse_rule.h"
#include "sql_parse/expr/expr_engine.h"
#include "sql_parse/expr/expr_registry.h"

namespace heterodb::sql_parse {

namespace {

bool MatchExprPrattCore(const ParseContext& ctx) {
  return ctx.expr_target != nullptr && !ctx.cursor.AtEnd();
}

Status ExprPluginPrattCore(ParseContext* ctx) {
  if (ctx == nullptr || ctx->expr_target == nullptr) {
    return Status::InvalidArgument("null expr parse target");
  }
  return ExprEngine::ParseExpr(&ctx->cursor, ctx->expr_target, ctx->expr_stop_tokens);
}

}  // namespace

void RegisterExprPlugins(ExprRegistry* registry) {
  if (registry == nullptr) {
    return;
  }
  ParseRule rule;
  rule.name = "ExprPlugin_PrattCore";
  rule.priority = 10;
  rule.match = MatchExprPrattCore;
  rule.handler = ExprPluginPrattCore;
  registry->plugins().Register(std::move(rule));
}

}  // namespace heterodb::sql_parse
