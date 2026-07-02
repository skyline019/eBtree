#pragma once

#include <string>
#include <unordered_map>

#include "sql/catalog/catalog.h"
#include "sql/catalog/row_codec.h"
#include "sql/eval/expr_eval.h"
#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {

Status ValidateRowConstraints(const TableSchema& table,
                              const std::unordered_map<std::string, std::string>& fields);

Status ValidateRowConstraintsWithEval(const TableSchema& table,
                                      const std::unordered_map<std::string, std::string>& fields,
                                      ExprEval* eval);

}  // namespace sql
}  // namespace ebtree
