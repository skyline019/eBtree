#pragma once

#include <vector>

#include "common/status.h"
#include "concept/query/expr.h"
#include "sql_parse/core/token_cursor.h"

namespace heterodb::sql_parse {

/** Pratt expression parser (migrated from concept/query/expr.cc). */
class ExprEngine {
 public:
  static Status ParseExpr(TokenCursor* cursor, Expr** out,
                          const std::vector<std::string>& stop_tokens);
};

}  // namespace heterodb::sql_parse
