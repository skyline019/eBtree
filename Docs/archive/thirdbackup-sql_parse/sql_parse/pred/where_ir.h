#pragma once

// WHERE representation invariants (Phase 8 single-track).
//
//   - where_expr  — canonical parse result (always set when WHERE present)
//   - where_and / where_or / key_range — optional derived fast-path predicates
//
// Single parse entry: pred::ParseWhere → ApplyWhereCanonicalize.

#include "concept/query/ast.h"

namespace heterodb::sql_parse::pred {

inline bool WhereUsesExprForm(const SqlStatement& stmt) {
  return stmt.where_expr != nullptr;
}

inline bool WhereUsesPredicateForm(const SqlStatement& stmt) {
  return !stmt.where_and.empty() || !stmt.where_or.empty();
}

}  // namespace heterodb::sql_parse::pred
