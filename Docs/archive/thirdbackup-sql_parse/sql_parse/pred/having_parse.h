#pragma once

#include <vector>
#include <string>

#include "common/status.h"
#include "concept/query/ast.h"
#include "sql_parse/core/parse_context.h"

namespace heterodb::sql_parse::pred {

/** Single entry for HAVING clause parsing (SELECT tail). */
Status ParseHaving(const std::vector<std::string>& tokens, size_t* pos,
                   SqlStatement* out);

Status ParseHaving(ParseContext* ctx);

}  // namespace heterodb::sql_parse::pred
