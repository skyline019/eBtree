#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sql/ast/minimal_ast.h"
#include "sql/ast/expr_ast.h"
#include "sql/ast/select_ast.h"
#include "sql/ast/advanced_ast.h"
#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {

enum class QueryStmtKind {
  kOpen,
  kCreateTable,
  kDropTable,
  kAlterTable,
  kCreateIndex,
  kDropIndex,
  kInsert,
  kUpdate,
  kDelete,
  kSelect,
  kBeginTxn,
  kCommit,
  kRollback,
  kSavepoint,
  kWithCte,
  kSetOp,
  kWindowSelect,
  kPrepare,
  kExecute,
  kUpsert,
  kCreateView,
  kDropView,
  kCreateTrigger,
  kDropTrigger,
  kReindex,
  kPragma,
  kShow,
  kSet,
  kGrant,
  kExplain,
  kUnknown,
};

struct JoinClause {
  std::string join_type{"INNER"};
  std::string table;
  std::string left_col;
  std::string right_col;
};

struct UpdateAssignment {
  std::string col;
  std::unique_ptr<ExprNode> expr;
};

struct QueryStatement {
  QueryStmtKind kind{QueryStmtKind::kUnknown};
  std::string raw_sql;
  OpenStmt open{};
  CreateTableStmt create_table{};
  struct {
    std::string name;
    std::string table;
    std::vector<std::string> columns;
    bool unique{false};
  } create_index{};
  std::string drop_index;
  std::string drop_table;
  struct {
    std::string table;
    std::string action;
  } alter_table;
  InsertStmt insert{};
  struct {
    std::string table;
    std::string key;
    std::string value;
    std::string conflict_action;
  } upsert{};
  struct {
    std::string name;
    std::string sql;
  } prepare{};
  struct {
    std::string name;
  } execute{};
  struct {
    std::string name;
    std::string base_table;
    std::string key_column;
    std::string value_column;
    std::shared_ptr<ExprNode> where_filter;
  } create_view{};
  std::string drop_view;
  struct {
    std::string name;
    std::string table;
    std::string event;
    std::string body_sql;
  } create_trigger{};
  std::string drop_trigger;
  std::string reindex_target;
  struct {
    std::string table;
    std::string set_col;
    std::string set_value;
    std::unique_ptr<ExprNode> set_expr;
    std::vector<UpdateAssignment> assignments;
    std::optional<std::string> where_col;
    std::optional<std::string> where_value;
    std::unique_ptr<ExprNode> where_expr;
  } update{};
  struct {
    std::string table;
    std::optional<std::string> where_col;
    std::optional<std::string> where_value;
    std::unique_ptr<ExprNode> where_expr;
  } delete_stmt{};
  SelectStmt select{};
  std::optional<SelectQuery> select_rich;
  std::optional<CteQuery> cte_query;
  std::optional<SetOpQuery> setop_query;
  std::optional<WindowQuery> window_query;
  std::vector<JoinClause> joins;
};

inline bool IsParseOnlyKind(QueryStmtKind k) {
  switch (k) {
    case QueryStmtKind::kShow:
    case QueryStmtKind::kSet:
    case QueryStmtKind::kGrant:
      return true;
    default:
      return false;
  }
}

Status FeatureNotSupported(const char* feature);

}  // namespace sql
}  // namespace ebtree
