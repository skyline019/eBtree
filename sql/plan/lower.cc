#include "exec_plan.h"

#include "sql/ast/expr_ast.h"
#include "sql/ast/select_ast.h"

namespace ebtree {
namespace sql {
namespace plan {

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
    const std::string table_name =
        in.select_rich ? in.select_rich->from_table : in.select.table;
    bool used_index = false;
    if (in.select_rich && in.select_rich->where) {
      const ExprNode* w = in.select_rich->where.get();
      if (w->kind == ExprKind::kBinary && w->bin_op == BinaryOp::kEq &&
          w->children.size() == 2 &&
          w->children[0]->kind == ExprKind::kColumn &&
          w->children[1]->kind == ExprKind::kLiteral) {
        PlanStep scan{};
        scan.kind = PlanStepKind::kIndexScan;
        scan.table = table_name;
        scan.index_column = w->children[0]->column;
        scan.index_value = w->children[1]->literal;
        out->steps.push_back(scan);
        used_index = true;
      }
    }
    if (!used_index) {
      PlanStep scan{};
      scan.kind = PlanStepKind::kTableScan;
      scan.table = table_name;
      out->steps.push_back(scan);
    }

    if (in.select_rich && in.select_rich->where) {
      PlanStep filter{};
      filter.kind = PlanStepKind::kFilter;
      out->steps.push_back(filter);
    }

    if (in.select_rich && !in.select_rich->joins.empty()) {
      for (const auto& j : in.select_rich->joins) {
        PlanStep join{};
        join.kind = PlanStepKind::kNestedLoopJoin;
        join.join_table = j.table;
        join.join_type = j.type;
        join.join_left_col = j.left_col;
        join.join_right_col = j.right_col;
        out->steps.push_back(join);
      }
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

    if (in.select_rich && !in.select_rich->group_by.empty()) {
      out->has_group_by = true;
      PlanStep agg{};
      agg.kind = PlanStepKind::kHashAggregate;
      out->steps.push_back(agg);
    }

    if (in.select_rich && !in.select_rich->order_by.empty()) {
      out->has_order_by = true;
      PlanStep sort{};
      sort.kind = PlanStepKind::kSortLimit;
      sort.limit = in.select_rich->limit;
      out->steps.push_back(sort);
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
