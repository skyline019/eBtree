#pragma once

#include <string>
#include <vector>

#include "sql/ast/minimal_ast.h"
#include "sql/catalog/catalog.h"
#include "sql/op_log/op_log_writer.h"
#include "sql/session/transaction_state.h"
#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace sql {

struct SqlRow {
  std::string key;
  std::string value;
};

struct ExecuteResult {
  std::vector<SqlRow> rows;
};

class SqlExecutor {
 public:
  SqlExecutor(Engine* engine, Catalog* catalog, OpLogWriter* op_log,
              DurabilityClass tier, TransactionState* txn = nullptr);

  Status Execute(const SqlStatement& stmt, ExecuteResult* out);

 private:
  Status ExecCreateTable(const CreateTableStmt& stmt);
  Status ExecInsert(const InsertStmt& stmt);
  Status ExecSelect(const SelectStmt& stmt, ExecuteResult* out);
  Status EngineGet(const std::string& key, std::string* value) const;
  Status EngineScan(const TypedPlan& plan,
                    std::vector<std::pair<std::string, std::string>>* rows) const;
  Status AppendOpLogPut(const std::string& encoded_key,
                        const std::string& value);
  bool DurableAtReturn() const;

  Engine* engine_{nullptr};
  Catalog* catalog_{nullptr};
  OpLogWriter* op_log_{nullptr};
  DurabilityClass tier_{DurabilityClass::kBalanced};
  TransactionState* txn_{nullptr};
};

}  // namespace sql
}  // namespace ebtree
