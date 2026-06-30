#include "sql_parse/pred/where_normalize.h"

namespace heterodb::sql_parse::pred {

void ApplyWhereNormalize(SqlStatement* stmt) {
  if (stmt == nullptr) {
    return;
  }
  // Phase 8: where_expr is canonical; derived where_and/key_range may coexist.
  if (stmt->where_expr != nullptr) {
    return;
  }
  if (!stmt->where_and.empty() || !stmt->where_or.empty() ||
      stmt->where.has_value() || stmt->key_range.has_value()) {
    stmt->where_expr.reset();
  }
}

void ApplyHavingNormalize(SqlStatement* stmt) {
  if (stmt == nullptr) {
    return;
  }
  // Phase 9: having_expr is canonical; derived having_and may coexist.
  if (stmt->having_expr != nullptr) {
    return;
  }
  if (!stmt->having_and.empty()) {
    stmt->having_expr.reset();
  }
}

}  // namespace heterodb::sql_parse::pred
