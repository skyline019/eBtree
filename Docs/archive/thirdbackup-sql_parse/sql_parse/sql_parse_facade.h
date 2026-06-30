#pragma once

#include <string>

#include "common/status.h"
#include "concept/query/ast.h"

namespace heterodb::sql_parse {

/** Registry-based SQL parse entry (parallel to heterodb::SqlParser). */
class SqlParseFacade {
 public:
  SqlParseFacade();
  ~SqlParseFacade();
  Status Parse(const std::string& sql, SqlStatement* out,
               const std::string& current_database = "default");
};

}  // namespace heterodb::sql_parse
