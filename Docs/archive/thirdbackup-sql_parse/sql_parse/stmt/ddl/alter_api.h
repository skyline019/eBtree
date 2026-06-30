#pragma once

// ParseConcept — ALTER TABLE action parsing.
// Layer: ParseConcept | Manifest: mysql-alter-batch

#include <cstdint>
#include <string>
#include <vector>

#include "common/status.h"
#include "concept/query/ast.h"

namespace heterodb::sql_parse {
namespace detail {

Status ParseAlterTable(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out);

}  // namespace detail
}  // namespace heterodb::sql_parse
