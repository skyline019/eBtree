#pragma once

#include <string>

#include "common/status.h"
#include "concept/query/ast.h"

namespace heterodb::sql_parse {

/** Production parse entry (SqlParseFacade when HETERODB_SQL_PARSE_NEXT). */
Status ParseSqlStatement(const std::string& sql, SqlStatement* out,
                         const std::string& current_database = "default");

/** Tokenize + thin router only; used by registry fallback and equiv tests. */
Status ParseSqlStatementViaRouter(const std::string& sql, SqlStatement* out,
                                  const std::string& current_database = "default");

}  // namespace heterodb::sql_parse
