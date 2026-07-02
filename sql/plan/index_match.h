#pragma once

#include <string>

#include "sql/ast/expr_ast.h"
#include "sql/catalog/catalog.h"

namespace ebtree {
namespace sql {
namespace plan {

enum class IndexScanMode { kNone, kEq, kRange };

struct IndexMatch {
  IndexScanMode mode{IndexScanMode::kNone};
  std::string index_column;
  std::string eq_value;
  std::string range_lo;
  std::string range_hi;
};

IndexMatch MatchIndexWhere(const ExprNode* where, const Catalog* catalog,
                           const std::string& table_name);

const IndexDef* FindLeadingIndex(const Catalog* catalog,
                                 const std::string& table_name,
                                 const std::string& column);

}  // namespace plan
}  // namespace sql
}  // namespace ebtree
