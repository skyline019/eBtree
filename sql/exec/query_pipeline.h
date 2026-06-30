#pragma once

#include "sql/ast/expr_ast.h"
#include "sql/eval/expr_eval.h"

namespace ebtree {
namespace sql {

class QueryPipeline {
 public:
  static bool PassesFilter(const ExprNode* where, const RowMap& row,
                           ExprEval* eval) {
    if (!where) return true;
    return eval->EvalBool(*where, row);
  }
};

}  // namespace sql
}  // namespace ebtree
