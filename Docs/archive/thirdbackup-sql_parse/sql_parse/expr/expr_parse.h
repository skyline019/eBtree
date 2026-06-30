#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common/status.h"
#include "concept/query/expr.h"

namespace heterodb::sql_parse {

Status ParseExpr(const std::vector<std::string>& tokens, size_t* pos, Expr** out);
bool IsExprStopToken(const std::string& tok);

}  // namespace heterodb::sql_parse
