#pragma once

#include "sql/ast/query_ast.h"
#include "sql/catalog/catalog.h"
#include "sql/catalog/catalog_store.h"
#include "sql/exec/executor.h"
#include "sql/op_log/op_log_writer.h"
#include "sql/session/transaction_state.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace sql {

class SqlExecutorV2 {
 public:
  SqlExecutorV2(Engine* engine, Catalog* catalog, CatalogStore* catalog_store,
                OpLogWriter* op_log, DurabilityClass tier,
                TransactionState* txn = nullptr);

  Status Execute(const QueryStatement& stmt, ExecuteResult* out);

  Status TryExecIndexScanSelect(const QueryStatement& stmt, ExecuteResult* out,
                                bool* handled);

 private:
  Status ExecDropTable(const std::string& table);
  Status ExecCreateIndex(const QueryStatement& stmt);
  Status ExecDropIndex(const std::string& name);
  Status ExecAlterTable(const QueryStatement& stmt);
  Status ExecUpdate(const QueryStatement& stmt);
  Status ExecDelete(const QueryStatement& stmt);
  Status ExecSelectJoin(const QueryStatement& stmt, ExecuteResult* out);
  Status ExecUpsert(const QueryStatement& stmt, ExecuteResult* out);
  Status ExecInsertSelect(const QueryStatement& stmt, ExecuteResult* out);
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
