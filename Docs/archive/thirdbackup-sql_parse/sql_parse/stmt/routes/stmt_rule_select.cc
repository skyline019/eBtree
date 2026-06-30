#include "sql_parse/core/parse_registry.h"
#include "sql_parse/stmt/priority.h"
#include "sql_parse/stmt/stmt_route_helpers.h"
#include "sql_parse/stmt/parse_stmt_handlers.h"
#include "sql_parse/stmt/stmt_route_helpers.h"

namespace heterodb::sql_parse {

void RegisterStmtRuleSelect(FirstMatchRegistry* routes) {
  if (routes == nullptr) {
    return;
  }
  using namespace stmt_priority;
  const ParseHandlerFn select_handler = DispatchSelectStatement;
  routes->Register(
      MakeRoute("StmtRule_With", kSyntaxPrefix, MatchHead("WITH"), select_handler));
  routes->Register(
      MakeRoute("StmtRule_Select", kStatement, MatchHead("SELECT"), select_handler));
}

}  // namespace heterodb::sql_parse
