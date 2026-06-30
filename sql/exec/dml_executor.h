#pragma once

#include "sql/ast/query_ast.h"
#include "sql/catalog/catalog.h"
#include "sql/catalog/catalog_store.h"
#include "sql/eval/expr_eval.h"
#include "sql/exec/executor.h"
#include "sql/exec/physical_scan.h"
#include "sql/exec/subquery_runner.h"
#include "sql/op_log/op_log_writer.h"
#include "sql/session/transaction_state.h"
#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace sql {

class DmlExecutor {
 public:
  DmlExecutor(ebtree::Engine* engine, Catalog* catalog,
              CatalogStore* catalog_store, OpLogWriter* op_log,
              DurabilityClass tier, TransactionState* txn);

  Status ExecUpdate(const QueryStatement& stmt);

 private:
  Status SaveCatalog() const;
  std::string ResolveSetValue(const ExprNode& expr, const RowMap& row,
                              const TableSchema& table) const;

  ebtree::Engine* engine_{nullptr};
  Catalog* catalog_{nullptr};
  CatalogStore* catalog_store_{nullptr};
  OpLogWriter* op_log_{nullptr};
  DurabilityClass tier_{DurabilityClass::kBalanced};
  TransactionState* txn_{nullptr};
  PhysicalScan scan_;
};

}  // namespace sql
}  // namespace ebtree
