#include "sql_parse/core/parse_context.h"
#include "sql_parse/core/parse_registry.h"
#include "sql_parse/stmt/priority.h"
#include "sql_parse/stmt/parse_stmt_handlers.h"
#include "sql_parse/stmt/stmt_route_helpers.h"

namespace heterodb::sql_parse {

void RegisterStmtRuleDml(FirstMatchRegistry* routes) {
  if (routes == nullptr) {
    return;
  }
  using namespace stmt_priority;
  const ParseHandlerFn dml_handler = DispatchDmlStatement;
  routes->Register(
      MakeRoute("StmtRule_Truncate", kStatement, MatchHead("TRUNCATE"), dml_handler));
  routes->Register(
      MakeRoute("StmtRule_Replace", kStatement, MatchHead("REPLACE"), dml_handler));
  routes->Register(
      MakeRoute("StmtRule_Insert", kStatement, MatchHead("INSERT"), dml_handler));
  routes->Register(
      MakeRoute("StmtRule_Update", kStatement, MatchHead("UPDATE"), dml_handler));
  routes->Register(
      MakeRoute("StmtRule_Delete", kStatement, MatchHead("DELETE"), dml_handler));
}

}  // namespace heterodb::sql_parse
