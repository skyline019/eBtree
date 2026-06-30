#pragma once

#include <string>

#include "sql/ast/expr_ast.h"
#include "sql/eval/sql_value.h"
#include "sql/eval/truth_value.h"

namespace ebtree {
namespace sql {

enum class TypeAffinity { kNull, kInteger, kReal, kText };

TypeAffinity AffinityOf(const std::string& value);
TypeAffinity AffinityOfValue(const SqlValue& value);
TypeAffinity AffinityFromColumnType(const std::string& col_type);

bool CompareWithAffinity(const std::string& lhs, const std::string& rhs,
                         BinaryOp op);

TruthValue CompareSqlValues(const SqlValue& lhs, const SqlValue& rhs,
                            BinaryOp op, TypeAffinity affinity_hint);

}  // namespace sql
}  // namespace ebtree
