// ParseConcept | SELECT — core orchestration.
#include "sql_parse/stmt/select/select_api.h"

#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {

static void ResetSelectStatement(SqlStatement* out) {
  out->kind = SqlStatementKind::kSelect;
  out->columns.clear();
  out->select_exprs.clear();
  out->where_expr.reset();
  out->derived_table_queries.clear();
  out->has_join = false;
  out->joins.clear();
  out->outer_table_names.clear();
  out->table_aliases.clear();
  out->primary_table_alias.clear();
  out->aggregate = AggregateFn::kNone;
  out->aggregate_column.reset();
  out->group_by_column.reset();
  out->group_by_columns.clear();
  out->group_by_exprs.clear();
  out->group_by_ordinals.clear();
  out->having_expr.reset();
  out->has_window = false;
  out->window_exprs.clear();
  out->window_defs.clear();
  out->key_range.reset();
  out->outer_table_names.clear();
  out->where_or.clear();
  out->where_and.clear();
  out->where.reset();
  out->order_by_items.clear();
  out->order_by_column.reset();
  out->order_by_desc = false;
}

Status ParseSelectStatement(const std::vector<std::string>& tokens, size_t* pos,
                            SqlStatement* out) {
  Status s = ParseSelectCore(tokens, pos, out);
  if (!s.ok()) {
    return s;
  }
  return ParseSelectTail(tokens, pos, out);
}

Status ParseSelectCore(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out) {
  ResetSelectStatement(out);

  if (*pos < tokens.size() && Upper(tokens[*pos]) == "SELECT") {
    ++(*pos);
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "DISTINCT") {
    ++(*pos);
    out->distinct = true;
  }

  Status s = ParseSelectProjectList(tokens, pos, out);
  if (!s.ok()) {
    return s;
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "FROM") {
    s = ParseSelectFromJoinWhere(tokens, pos, out);
    if (!s.ok()) {
      return s;
    }
  } else {
    out->select_no_from = true;
    out->table.clear();
    out->catalog_table = false;
  }
  return ParseSelectGroupHaving(tokens, pos, out);
}


}  // namespace detail
}  // namespace heterodb::sql_parse
