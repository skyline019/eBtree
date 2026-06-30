#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sql/ast/select_ast.h"

namespace ebtree {
namespace sql {

struct CteEntry {
  std::string name;
  std::unique_ptr<SelectQuery> query;
};

struct CteQuery {
  bool recursive{false};
  std::vector<CteEntry> ctes;
  std::unique_ptr<SelectQuery> main_query;
};

enum class SetOpKind { kUnion, kIntersect, kExcept };

struct SetOpQuery {
  SetOpKind op{SetOpKind::kUnion};
  bool all{false};
  std::unique_ptr<SelectQuery> left;
  std::unique_ptr<SelectQuery> right;
};

struct WindowQuery {
  std::string window_func;
  std::string partition_col;
  std::string order_col;
  bool order_desc{false};
  std::unique_ptr<SelectQuery> query;
};

}  // namespace sql
}  // namespace ebtree
