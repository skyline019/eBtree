#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sql/ast/select_ast.h"
#include "sql/ast/query_ast.h"
#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {
namespace plan {

enum class PlanStepKind {
  kTableScan,
  kIndexScan,
  kFilter,
  kNestedLoopJoin,
  kHashAggregate,
  kSortLimit,
  kProject,
};

struct PlanStep {
  PlanStepKind kind{PlanStepKind::kTableScan};
  std::string table;
  std::string index_name;
  std::string index_column;
  std::string index_value;
  std::string join_table;
  JoinType join_type{JoinType::kInner};
  std::string join_left_col;
  std::string join_right_col;
  std::optional<uint64_t> limit;
};

struct ExecPlan {
  std::vector<PlanStep> steps;
  bool has_group_by{false};
  bool has_order_by{false};
  bool has_subquery{false};
};

Status LowerQuery(const QueryStatement& in, ExecPlan* out);

}  // namespace plan
}  // namespace sql
}  // namespace ebtree
