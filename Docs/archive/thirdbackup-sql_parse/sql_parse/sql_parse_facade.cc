#include "sql_parse/sql_parse_facade.h"

#include "sql_parse/bootstrap/parse_bootstrap.h"

namespace heterodb::sql_parse {

SqlParseFacade::SqlParseFacade() = default;

SqlParseFacade::~SqlParseFacade() = default;

Status SqlParseFacade::Parse(const std::string& sql, SqlStatement* out,
                             const std::string& current_database) {
  return ParseBootstrap::Global().Parse(sql, out, current_database);
}

}  // namespace heterodb::sql_parse
