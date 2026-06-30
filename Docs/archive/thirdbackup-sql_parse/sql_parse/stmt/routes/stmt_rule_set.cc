#include "sql_parse/core/parse_registry.h"
#include "sql_parse/stmt/priority.h"
#include "sql_parse/stmt/stmt_route_helpers.h"
#include "sql_parse/stmt/parse_stmt_handlers.h"
#include "sql_parse/stmt/stmt_route_helpers.h"

namespace heterodb::sql_parse {

void RegisterStmtRuleSet(FirstMatchRegistry* routes) {
  if (routes == nullptr) {
    return;
  }
  using namespace stmt_priority;
  routes->Register(
      MakeRoute("StmtRule_Set", kStatement, MatchHead("SET"), DispatchSetStatement));
}

}  // namespace heterodb::sql_parse
