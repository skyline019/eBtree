#include "sql_parse/core/parse_registry.h"
#include "sql_parse/stmt/priority.h"
#include "sql_parse/stmt/stmt_route_helpers.h"
#include "sql_parse/stmt/parse_stmt_handlers.h"
#include "sql_parse/stmt/stmt_route_helpers.h"

namespace heterodb::sql_parse {

void RegisterStmtRuleDdl(FirstMatchRegistry* routes) {
  if (routes == nullptr) {
    return;
  }
  using namespace stmt_priority;
  const ParseHandlerFn ddl_handler = DispatchDdlStatement;
  routes->Register(
      MakeRoute("StmtRule_Create", kStatement, MatchHead("CREATE"), ddl_handler));
  routes->Register(
      MakeRoute("StmtRule_Drop", kStatement, MatchHead("DROP"), ddl_handler));
  routes->Register(
      MakeRoute("StmtRule_Alter", kStatement, MatchHead("ALTER"), ddl_handler));
  routes->Register(
      MakeRoute("StmtRule_Reindex", kStatement, MatchHead("REINDEX"), ddl_handler));
}

}  // namespace heterodb::sql_parse
