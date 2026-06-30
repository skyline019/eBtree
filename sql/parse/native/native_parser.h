#pragma once

#include "ebtree/common/status.h"
#include "sql/ast/query_ast.h"

namespace ebtree {
namespace sql {
namespace parse {

class NativeParser {
 public:
  Status Parse(const std::string& sql, QueryStatement* out) const;
};

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
