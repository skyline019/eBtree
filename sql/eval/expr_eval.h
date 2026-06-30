#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "sql/ast/expr_ast.h"
#include "sql/eval/schema_context.h"
#include "sql/eval/sql_value.h"
#include "sql/eval/truth_value.h"
#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {

using RowMap = std::unordered_map<std::string, std::string>;
using SubqueryTruthFn =
    std::function<TruthValue(const ExprNode& node, const RowMap& outer_row)>;

class ExprEval {
 public:
  void SetSchemaContext(SchemaContext ctx) { schema_ = std::move(ctx); }
  void SetSubqueryEval(SubqueryTruthFn fn) { subquery_fn_ = std::move(fn); }

  SqlValue EvalValue(const ExprNode& node, const RowMap& row) const;
  TruthValue EvalTruth(const ExprNode& node, const RowMap& row) const;

  bool EvalBool(const ExprNode& node, const RowMap& row) const;
  std::string EvalScalar(const ExprNode& node, const RowMap& row) const;
  TruthValue EvalInLiterals(const SqlValue& lhs,
                            const std::vector<std::string>& lits,
                            bool not_op) const;

 private:
  std::string LookupColumn(const ExprNode& node, const RowMap& row) const;
  TypeAffinity ColumnAffinityFor(const ExprNode& node) const;
  TruthValue EvalSubqueryTruth(const ExprNode& node, const RowMap& row) const;
  SqlValue EvalBinaryValue(const ExprNode& node, const RowMap& row) const;

  SchemaContext schema_;
  SubqueryTruthFn subquery_fn_;
};

}  // namespace sql
}  // namespace ebtree
