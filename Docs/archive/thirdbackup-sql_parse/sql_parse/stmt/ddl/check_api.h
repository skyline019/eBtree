#pragma once

#include <string>
#include <vector>

#include "common/status.h"
#include "concept/catalog/catalog_types.h"

namespace heterodb::sql_parse {
namespace detail {

Status ParseCheckConstraintBody(const std::vector<std::string>& tokens, size_t* pos,
                                const std::string& name, CheckConstraintDef* chk);

std::string JoinCheckExprTokens(const std::vector<std::string>& tokens, size_t begin,
                                size_t end);

}  // namespace detail
}  // namespace heterodb::sql_parse
