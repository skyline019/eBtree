#pragma once

#include "sql/ast/query_ast.h"
#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {
namespace parse {

class RegistryParser {
 public:
  Status Parse(const std::string& sql, QueryStatement* out) const;
};

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
