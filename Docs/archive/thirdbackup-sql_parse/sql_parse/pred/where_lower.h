#pragma once

#include "concept/catalog/catalog_types.h"
#include "concept/query/ast.h"

namespace heterodb::sql_parse::pred {

/// Phase 8: canonical WHERE — build `where_expr` from legacy predicates when missing,
/// then derive index-friendly `where_and` / `where_or` / `key_range` from `where_expr`.
void ApplyWhereCanonicalize(SqlStatement* stmt);

void ApplyHavingCanonicalize(SqlStatement* stmt);

}  // namespace heterodb::sql_parse::pred
