#include "sql_parse/core/parse_context.h"
#include "sql_parse/core/parse_registry.h"
#include "sql_parse/stmt/priority.h"
#include "sql_parse/stmt/parse_stmt_handlers.h"
#include "sql_parse/stmt/stmt_route_helpers.h"

namespace heterodb::sql_parse {

void RegisterStmtRuleMeta(FirstMatchRegistry* routes) {
  if (routes == nullptr) {
    return;
  }
  using namespace stmt_priority;
  const ParseHandlerFn meta_handler = DispatchMetaStatement;
  routes->Register(
      MakeRoute("StmtRule_Use", kStatement, MatchHead("USE"), meta_handler));
  routes->Register(MakeRoute("StmtRule_Show", kStatement, MatchHead("SHOW"),
                             meta_handler));
  routes->Register(MakeRoute("StmtRule_Describe", kKeywordAlias,
                             MatchHeadAny({"DESCRIBE", "DESC"}),
                             meta_handler));
  routes->Register(MakeRoute("StmtRule_Explain", kSyntaxPrefix, MatchHead("EXPLAIN"),
                             meta_handler));
}

}  // namespace heterodb::sql_parse
