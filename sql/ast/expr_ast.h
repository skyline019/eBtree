#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ebtree {
namespace sql {

struct SelectQuery;
struct ExprNode;

enum class ExprKind {
  kLiteral,
  kColumn,
  kBinary,
  kUnary,
  kIsNull,
  kFunction,
  kSubquery,
};

enum class BinaryOp {
  kAnd,
  kOr,
  kEq,
  kNe,
  kLt,
  kLe,
  kGt,
  kGe,
  kLike,
  kAdd,
  kSub,
  kMul,
  kDiv,
};

struct SubquerySpec {
  bool exists{false};
  bool scalar{false};
  bool not_op{false};
  bool correlated{false};
  std::string sql;
  std::unique_ptr<SelectQuery> parsed_query;
  std::unique_ptr<ExprNode> lhs;
  std::vector<std::string> in_literals;
};

struct ExprNode {
  ExprKind kind{ExprKind::kLiteral};
  std::string literal;
  std::string column;
  std::string table;
  BinaryOp bin_op{BinaryOp::kEq};
  std::string func_name;
  std::vector<std::unique_ptr<ExprNode>> children;
  bool is_null_check{false};
  bool is_not{false};
  std::optional<SubquerySpec> subquery;
};

struct AggregateSpec {
  std::string func;
  std::string column;
  std::string alias;
  bool distinct{false};
  std::string separator{","};
};

}  // namespace sql
}  // namespace ebtree
