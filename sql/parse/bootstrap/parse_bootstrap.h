#pragma once

#include "sql/ast/query_ast.h"
#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {
namespace parse {

class ParseBootstrap {
 public:
  static ParseBootstrap& Global();
  Status Parse(const std::string& sql, QueryStatement* out) const;

 private:
  ParseBootstrap() = default;
};

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
