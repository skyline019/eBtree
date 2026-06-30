#include "sql_parse/core/parse_registry.h"
#include "sql_parse/stmt/priority.h"
#include "sql_parse/stmt/stmt_route_helpers.h"
#include "sql_parse/stmt/parse_stmt_handlers.h"
#include "sql_parse/stmt/stmt_route_helpers.h"

namespace heterodb::sql_parse {

void RegisterStmtRulePriv(FirstMatchRegistry* routes) {
  if (routes == nullptr) {
    return;
  }
  using namespace stmt_priority;
  const ParseHandlerFn priv_handler = DispatchPrivStatement;
  routes->Register(
      MakeRoute("StmtRule_Grant", kStatement, MatchHead("GRANT"), priv_handler));
  routes->Register(
      MakeRoute("StmtRule_Revoke", kStatement, MatchHead("REVOKE"), priv_handler));
}

}  // namespace heterodb::sql_parse
