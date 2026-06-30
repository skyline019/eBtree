#pragma once

// ParseConcept — JOIN ON condition parsing (composite equality AND).
// Layer: ParseConcept | Manifest: join-inner

#include <string>
#include <vector>

#include "common/status.h"
#include "concept/query/ast.h"

namespace heterodb::sql_parse {
namespace detail {

Status ParseJoinOnCondition(const std::vector<std::string>& tokens, size_t* pos,
                            JoinClause* join);

}  // namespace detail
}  // namespace heterodb::sql_parse
