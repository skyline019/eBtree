#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "sql/ast/query_ast.h"
#include "sql/catalog/catalog.h"
#include "sql/catalog/catalog_store.h"
#include "sql/exec/executor.h"
#include "sql/exec/unified_executor.h"
#include "sql/op_log/op_log_writer.h"
#include "sql/parse/registry/registry_parser.h"
#include "sql/session/transaction_state.h"
#include "sql/session/open_options.h"
#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"
#include "rar_monitor.h"

namespace ebtree {
namespace sql {

class Database {
 public:
  static Status Open(const OpenOptions& opts, std::unique_ptr<Database>* out);

  Status ExecuteSql(const std::string& sql, ExecuteResult* result = nullptr);
  Status Execute(const QueryStatement& stmt, ExecuteResult* result = nullptr);

  const std::string& last_error() const { return last_error_; }

  Engine* engine() { return engine_.get(); }
  const OpenOptions& options() const { return options_; }
  Catalog* catalog() { return &catalog_; }
  const audit::RarMonitor& rar_monitor() const { return rar_monitor_; }

  void Close();

 private:
  Database(OpenOptions opts, std::unique_ptr<Engine> engine);
  void InstallGroupCommitObserver();
  void InstallRarMonitor();

  OpenOptions options_;
  std::unique_ptr<Engine> engine_;
  Catalog catalog_;
  CatalogStore catalog_store_;
  std::unique_ptr<OpLogWriter> op_log_;
  parse::RegistryParser parser_;
  TransactionState txn_;
  UnifiedExecutor executor_;
  audit::RarMonitor rar_monitor_;
  std::unordered_map<std::string, std::string> prepared_;
  std::string last_error_;
};

}  // namespace sql
}  // namespace ebtree
