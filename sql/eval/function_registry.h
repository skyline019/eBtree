#pragma once

#include <string>

#include "sql/ast/expr_ast.h"
#include "sql/eval/expr_eval.h"
#include "sql/eval/sql_value.h"

namespace ebtree {
namespace sql {

class FunctionRegistry {
 public:
  static SqlValue Eval(const std::string& name, const ExprNode& node,
                       const RowMap& row, const ExprEval* eval);
};

}  // namespace sql
}  // namespace ebtree
