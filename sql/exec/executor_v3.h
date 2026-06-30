#pragma once

#include "sql/session/transaction_state.h"
#include "sql/ast/query_ast.h"
#include "sql/catalog/catalog.h"
#include "sql/catalog/catalog_store.h"
#include "sql/exec/cte_context.h"
#include "sql/exec/executor.h"
#include "sql/exec/subquery_runner.h"
#include "sql/op_log/op_log_writer.h"
#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace sql {

class SqlExecutorV3 {
 public:
  SqlExecutorV3(Engine* engine, Catalog* catalog, CatalogStore* catalog_store,
                OpLogWriter* op_log, DurabilityClass tier,
                TransactionState* txn = nullptr);

  Status Execute(const QueryStatement& stmt, ExecuteResult* out);

 private:
  Status ExecSelect(const QueryStatement& stmt, ExecuteResult* out);
  Status ExecSelectRich(const SelectQuery& sq, const TableSchema& table,
                        ExecuteResult* out, const CteContext* cte_ctx = nullptr,
                        const ExprNode* extra_where = nullptr);
  Status CollectSelectRows(const SelectQuery& sq, const CteContext* cte_ctx,
                           std::vector<RowMap>* rows);
  Status ExecCte(const QueryStatement& stmt, ExecuteResult* out);
  Status ExecSetOp(const QueryStatement& stmt, ExecuteResult* out);
  Status ExecWindow(const QueryStatement& stmt, ExecuteResult* out);
  Status ExecExplain(const QueryStatement& stmt, ExecuteResult* out);
  Status ScanTableRows(const TableSchema& table,
                       std::vector<std::pair<std::string, std::string>>* rows);
  Status SaveCatalog() const;

  Engine* engine_{nullptr};
  Catalog* catalog_{nullptr};
  CatalogStore* catalog_store_{nullptr};
  OpLogWriter* op_log_{nullptr};
  DurabilityClass tier_{DurabilityClass::kBalanced};
  TransactionState* txn_{nullptr};
  SqlExecutor base_;
};

}  // namespace sql
}  // namespace ebtree
