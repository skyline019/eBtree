#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/status.h"
#include "sql/ast/select_ast.h"
#include "ebtree/engine/engine.h"

#include "sql/eval/truth_value.h"
#include "sql/ast/expr_ast.h"
#include "sql/exec/cte_context.h"

namespace ebtree {
namespace sql {

class Catalog;

class SubqueryRunner {
 public:
  using SubqueryTruthFn = std::function<TruthValue(const ExprNode&, const RowMap&)>;

  SubqueryRunner(ebtree::Engine* engine, Catalog* catalog);

  void SetSubqueryEval(SubqueryTruthFn fn) { subquery_fn_ = std::move(fn); }
  void SetCteContext(const CteContext* ctx) { cte_ctx_ = ctx; }
  void SetMaxPages(std::optional<uint64_t> max_pages) { max_pages_ = max_pages; }

  Status LastError() const { return last_error_; }
  void ClearLastError() { last_error_ = Status::Ok(); }

  static Status ValidateWhereDepth(const ExprNode& where, int parent_depth = 0);

  Status Run(const SelectQuery& query, const RowMap& outer_row,
             std::vector<RowMap>* out, int level = 0) const;

  TruthValue EvalSubqueryTruth(const ExprNode& node, const RowMap& outer,
                               int parent_depth) const;

 private:
  Status ScanTable(const struct TableSchema& table,
                   std::vector<std::pair<std::string, std::string>>* rows) const;
  void PopulateFields(const struct TableSchema& table, const std::string& user_key,
                      const std::string& raw, RowMap* fields) const;
  bool ExistsMatch(const SelectQuery& query, const RowMap& outer_row,
                   int level) const;
  bool EvalWhere(const ExprNode& node, const RowMap& row, int level) const;

  ebtree::Engine* engine_{nullptr};
  Catalog* catalog_{nullptr};
  const CteContext* cte_ctx_{nullptr};
  std::optional<uint64_t> max_pages_;
  mutable Status last_error_{Status::Ok()};
  SubqueryTruthFn subquery_fn_;
};

}  // namespace sql
}  // namespace ebtree
