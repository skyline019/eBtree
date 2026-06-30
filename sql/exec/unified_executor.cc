#include "unified_executor.h"

#include "sql/exec/dml_executor.h"
#include "sql/exec/executor_v2.h"

namespace ebtree {
namespace sql {

UnifiedExecutor::UnifiedExecutor(ebtree::Engine* engine, Catalog* catalog,
                                 CatalogStore* catalog_store,
                                 OpLogWriter* op_log, DurabilityClass tier,
                                 TransactionState* txn)
    : engine_(engine),
      catalog_(catalog),
      catalog_store_(catalog_store),
      op_log_(op_log),
      tier_(tier),
      txn_(txn),
      select_exec_(engine, catalog, catalog_store, op_log, tier, txn) {}

Status UnifiedExecutor::Execute(const QueryStatement& stmt, ExecuteResult* out) {
  if (stmt.kind == QueryStmtKind::kUpdate) {
    DmlExecutor dml(engine_, catalog_, catalog_store_, op_log_, tier_, txn_);
    return dml.ExecUpdate(stmt);
  }
  return select_exec_.Execute(stmt, out);
}

}  // namespace sql
}  // namespace ebtree
