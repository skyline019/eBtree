#include "sql_parse/stmt/parse_statement.h"

#include "concept/schema/schema.h"
#include "sql_parse/core/parse_context.h"
#include "sql_parse/shared/parse_shared.h"
#include "sql_parse/stmt/dispatch/dispatch_api.h"
#include "sql_parse/stmt/parse_common_api.h"

#if defined(HETERODB_SQL_PARSE_NEXT)
#include "sql_parse/sql_parse_facade.h"
#endif

namespace heterodb::sql_parse {

Status ParseSqlStatementViaRouter(const std::string& sql, SqlStatement* out,
                                  const std::string& current_database) {
  if (out == nullptr) {
    return Status::InvalidArgument("null output");
  }
  detail::ParseSessionScope session(
      current_database.empty() ? kDefaultDatabaseName : current_database);
  std::string normalized_storage;
  const std::string& token_input =
      SqlInputNeedsNormalization(sql) ? (normalized_storage = NormalizeSqlInput(sql),
                                         normalized_storage)
                                      : sql;
  const std::vector<std::string> tokens = SplitTokens(token_input);
  return detail::ParseStatementFromTokens(tokens, out);
}

Status ParseSqlStatement(const std::string& sql, SqlStatement* out,
                         const std::string& current_database) {
#if defined(HETERODB_SQL_PARSE_NEXT)
  SqlParseFacade facade;
  return facade.Parse(sql, out, current_database);
#else
  return ParseSqlStatementViaRouter(sql, out, current_database);
#endif
}

}  // namespace heterodb::sql_parse
