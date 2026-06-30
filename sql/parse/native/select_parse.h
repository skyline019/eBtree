#pragma once

#include "ebtree/common/status.h"
#include "sql/ast/select_ast.h"
#include "sql/parse/core/token_cursor.h"

namespace ebtree {
namespace sql {
namespace parse {

class SelectParse {
 public:
  Status ParseSelect(const std::string& raw_sql, TokenCursor* cur,
                     SelectQuery* out);
};

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
