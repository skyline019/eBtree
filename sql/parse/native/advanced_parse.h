#pragma once

#include "ebtree/common/status.h"
#include "sql/ast/query_ast.h"

namespace ebtree {
namespace sql {
namespace parse {

Status ParseCteQuery(const std::string& raw_sql, QueryStatement* out);
Status ParseSetOpQuery(const std::string& raw_sql, QueryStatement* out);
Status ParseWindowQuery(const std::string& raw_sql, QueryStatement* out);

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
