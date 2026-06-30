#include "sql_parse/stmt/stmt_route_helpers.h"

#include "sql_parse/core/parse_registry.h"
#include "sql_parse/stmt/priority.h"
#include "sql_parse/stmt/parse_stmt_handlers.h"

namespace heterodb::sql_parse {

void RegisterStmtRuleTxn(FirstMatchRegistry* routes) {
  if (routes == nullptr) {
    return;
  }
  using namespace stmt_priority;
  const ParseHandlerFn txn_handler = DispatchTxnStatement;
  routes->Register(MakeRoute("StmtRule_Begin", kStatement, MatchHeadAny({"BEGIN", "START"}),
                             txn_handler));
  routes->Register(
      MakeRoute("StmtRule_Commit", kStatement, MatchHead("COMMIT"), txn_handler));
  routes->Register(MakeRoute("StmtRule_Savepoint", kStatement, MatchHead("SAVEPOINT"),
                             txn_handler));
  routes->Register(
      MakeRoute("StmtRule_Rollback", kStatement, MatchHead("ROLLBACK"), txn_handler));
  routes->Register(MakeRoute("StmtRule_Release", kStatement, MatchHead("RELEASE"),
                             txn_handler));
}

}  // namespace heterodb::sql_parse
