#pragma once

#include "sql/ast/query_ast.h"
#include "sql/catalog/catalog.h"
#include "sql/catalog/catalog_store.h"
#include "sql/exec/executor.h"
#include "sql/exec/executor_v3.h"
#include "sql/op_log/op_log_writer.h"
#include "sql/session/transaction_state.h"
#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace sql {

// Unified SQL execution entry: SELECT pipeline + DML routing.
class UnifiedExecutor {
 public:
  UnifiedExecutor(ebtree::Engine* engine, Catalog* catalog,
                  CatalogStore* catalog_store, OpLogWriter* op_log,
                  DurabilityClass tier, TransactionState* txn = nullptr);

  Status Execute(const QueryStatement& stmt, ExecuteResult* out);

 private:
  ebtree::Engine* engine_{nullptr};
  Catalog* catalog_{nullptr};
  CatalogStore* catalog_store_{nullptr};
  OpLogWriter* op_log_{nullptr};
  DurabilityClass tier_{DurabilityClass::kBalanced};
  TransactionState* txn_{nullptr};
  SqlExecutorV3 select_exec_;
};

}  // namespace sql
}  // namespace ebtree
