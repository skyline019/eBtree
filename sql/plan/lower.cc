#include "exec_plan.h"

#include "sql/ast/expr_ast.h"
#include "sql/ast/select_ast.h"
#include "sql/plan/index_match.h"

namespace ebtree {
namespace sql {
namespace plan {

namespace {

void LowerSelectBody(const SelectQuery* select_rich, const std::string& legacy_table,
                     const Catalog* catalog, ExecPlan* out) {
  const std::string table_name =
      select_rich ? select_rich->from_table : legacy_table;
  bool used_index = false;
  if (select_rich && select_rich->where && catalog && !table_name.empty()) {
    const IndexMatch match =
        MatchIndexWhere(select_rich->where.get(), catalog, table_name);
    if (match.mode == IndexScanMode::kEq) {
      PlanStep scan{};
      scan.kind = PlanStepKind::kIndexScan;
      scan.table = table_name;
      scan.index_column = match.index_column;
      scan.index_value = match.eq_value;
      scan.index_scan_mode = IndexScanMode::kEq;
      out->steps.push_back(scan);
      used_index = true;
    } else if (match.mode == IndexScanMode::kRange) {
      PlanStep scan{};
      scan.kind = PlanStepKind::kIndexScan;
      scan.table = table_name;
      scan.index_column = match.index_column;
      scan.index_range_lo = match.range_lo;
      scan.index_range_hi = match.range_hi;
      scan.index_scan_mode = IndexScanMode::kRange;
      out->steps.push_back(scan);
      used_index = true;
    }
  }
  if (!used_index) {
    PlanStep scan{};
    scan.kind = PlanStepKind::kTableScan;
    scan.table = table_name;
    out->steps.push_back(scan);
    if (select_rich && select_rich->where) {
      PlanStep filter{};
      filter.kind = PlanStepKind::kFilter;
      out->steps.push_back(filter);
    }
  }

  if (select_rich && !select_rich->joins.empty()) {
    for (const auto& j : select_rich->joins) {
      PlanStep join{};
      join.kind = PlanStepKind::kNestedLoopJoin;
      join.join_table = j.table;
      join.join_type = j.type;
      join.join_left_col = j.left_col;
      join.join_right_col = j.right_col;
      out->steps.push_back(join);
    }
  }

  if (select_rich && !select_rich->group_by.empty()) {
    out->has_group_by = true;
    PlanStep agg{};
    agg.kind = PlanStepKind::kHashAggregate;
    out->steps.push_back(agg);
  }

  if (select_rich && !select_rich->order_by.empty()) {
    out->has_order_by = true;
    PlanStep sort{};
    sort.kind = PlanStepKind::kSortLimit;
    sort.limit = select_rich->limit;
    out->steps.push_back(sort);
  }
}

}  // namespace

Status LowerSelectQuery(const SelectQuery& sq, const Catalog* catalog,
                        ExecPlan* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->steps.clear();
  out->has_group_by = false;
  out->has_order_by = false;
  out->has_subquery = false;
  LowerSelectBody(&sq, sq.from_table, catalog, out);
  return Status::Ok();
}

Status LowerQuery(const QueryStatement& in, ExecPlan* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->steps.clear();
  out->has_group_by = false;
  out->has_order_by = false;
  out->has_subquery = false;

  if (IsParseOnlyKind(in.kind)) {
    return Status::Ok();
  }

  if (in.kind == QueryStmtKind::kSelect) {
    LowerSelectBody(in.select_rich.has_value() ? &(*in.select_rich) : nullptr,
                    in.select.table, nullptr, out);
    if (in.select_rich && !in.select_rich->joins.empty()) {
      // joins already handled in LowerSelectBody when select_rich set
    } else if (!in.joins.empty()) {
      for (const auto& j : in.joins) {
        PlanStep join{};
        join.kind = PlanStepKind::kNestedLoopJoin;
        join.join_table = j.table;
        join.join_left_col = j.left_col;
        join.join_right_col = j.right_col;
        out->steps.push_back(join);
      }
    }
  }

  if (in.update.where_expr || in.delete_stmt.where_expr) {
    out->has_subquery = true;
  }

  return Status::Ok();
}

Status LowerQueryWithCatalog(const QueryStatement& in, const Catalog* catalog,
                             ExecPlan* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->steps.clear();
  out->has_group_by = false;
  out->has_order_by = false;
  out->has_subquery = false;

  if (IsParseOnlyKind(in.kind)) {
    return Status::Ok();
  }

  if (in.kind == QueryStmtKind::kSelect) {
    LowerSelectBody(in.select_rich.has_value() ? &(*in.select_rich) : nullptr,
                    in.select.table, catalog, out);
    if (!in.select_rich && !in.joins.empty()) {
      for (const auto& j : in.joins) {
        PlanStep join{};
        join.kind = PlanStepKind::kNestedLoopJoin;
        join.join_table = j.table;
        join.join_left_col = j.left_col;
        join.join_right_col = j.right_col;
        out->steps.push_back(join);
      }
    }
  }

  if (in.update.where_expr || in.delete_stmt.where_expr) {
    out->has_subquery = true;
  }

  return Status::Ok();
}

}  // namespace plan
}  // namespace sql
}  // namespace ebtree
