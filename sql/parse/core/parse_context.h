#pragma once

#include <string>
#include <vector>

#include "sql/ast/query_ast.h"
#include "sql/parse/core/token_cursor.h"

namespace ebtree {
namespace sql {
namespace parse {

struct ParseContext {
  std::string raw_sql;
  std::vector<std::string> tokens;
  TokenCursor cursor;
  QueryStatement* out{nullptr};

  void ResetTokens(std::vector<std::string> t) {
    tokens = std::move(t);
    cursor.Reset(tokens);
  }
};

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
