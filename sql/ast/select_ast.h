#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sql/ast/expr_ast.h"

namespace ebtree {
namespace sql {

enum class JoinType { kInner, kLeft, kRight, kFull, kCross };

struct JoinSpec {
  JoinType type{JoinType::kInner};
  std::string table;
  std::string left_table;
  std::string left_col;
  std::string right_table;
  std::string right_col;
};

struct OrderSpec {
  std::string column;
  bool descending{false};
};

struct SelectQuery {
  std::vector<std::string> project_cols;
  std::string from_table;
  std::vector<JoinSpec> joins;
  std::unique_ptr<ExprNode> where;
  std::vector<std::string> group_by;
  std::unique_ptr<ExprNode> having;
  std::vector<OrderSpec> order_by;
  std::optional<uint64_t> limit;
  std::optional<uint64_t> offset;
  bool distinct{false};
  std::vector<AggregateSpec> aggregates;
  std::vector<std::unique_ptr<ExprNode>> scalar_projects;
  std::optional<std::string> eq_key;
  std::optional<uint64_t> max_pages;
};

}  // namespace sql
}  // namespace ebtree
