#pragma once

#include <vector>
#include <string>

#include "common/status.h"
#include "concept/query/ast.h"
#include "sql_parse/core/parse_context.h"

namespace heterodb::sql_parse::pred {

/** Single entry for WHERE clause parsing (native + facade). */
Status ParseWhere(const std::vector<std::string>& tokens, size_t* pos,
                  SqlStatement* out);

Status ParseWhere(ParseContext* ctx);

}  // namespace heterodb::sql_parse::pred
