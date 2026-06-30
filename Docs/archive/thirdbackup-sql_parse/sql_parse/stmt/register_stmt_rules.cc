#include "sql_parse/bootstrap/parse_module.h"

#include "concept/schema/schema.h"
#include "sql_parse/core/parse_context.h"
#include "sql_parse/stmt/dispatch/dispatch_api.h"
#include "sql_parse/stmt/parse_common_api.h"
#include "sql_parse/stmt/routes/stmt_routes.h"
#include "sql_parse/stmt/stmt_registry.h"

namespace heterodb::sql_parse {

void RegisterStmtRoutes(StmtRegistry* registry) {
  if (registry == nullptr) {
    return;
  }
  FirstMatchRegistry& routes = registry->routes();
  RegisterStmtRuleSelect(&routes);
  RegisterStmtRuleDml(&routes);
  RegisterStmtRuleDdl(&routes);
  RegisterStmtRuleTxn(&routes);
  RegisterStmtRuleMeta(&routes);
  RegisterStmtRulePriv(&routes);
  RegisterStmtRuleSet(&routes);
}

void InstallRouterStmtFallback(FirstMatchRegistry* routes) {
  if (routes == nullptr) {
    return;
  }
  routes->Register(
      {"StmtParseFallback", 0,
       [](const ParseContext& ctx) {
         return ctx.out != nullptr && !ctx.cursor.AtEnd();
       },
       [](ParseContext* ctx) {
         if (ctx == nullptr || ctx->out == nullptr || ctx->cursor.AtEnd()) {
           return Status::InvalidArgument("invalid parse context");
         }
         detail::ParseSessionScope session(
             ctx->current_database.empty() ? std::string(kDefaultDatabaseName)
                                           : ctx->current_database,
             ctx->catalog_bridge);
         return detail::ParseStatementFromTokens(ctx->cursor.tokens(), ctx->out);
       }});
}

}  // namespace heterodb::sql_parse
