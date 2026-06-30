#pragma once

#include <memory>

#include "ebtree/common/status.h"
#include "sql/ast/expr_ast.h"
#include "sql/parse/core/token_cursor.h"

namespace ebtree {
namespace sql {
namespace parse {

class ExprParse {
 public:
  Status ParsePredicate(TokenCursor* cur, std::unique_ptr<ExprNode>* out);
  Status ParseExpr(TokenCursor* cur, std::unique_ptr<ExprNode>* out);
  Status ParseAdd(TokenCursor* cur, std::unique_ptr<ExprNode>* out);
  Status ParsePrimary(TokenCursor* cur, std::unique_ptr<ExprNode>* out);

 private:
  Status ParseOr(TokenCursor* cur, std::unique_ptr<ExprNode>* out);
  Status ParseAnd(TokenCursor* cur, std::unique_ptr<ExprNode>* out);
  Status ParseComparison(TokenCursor* cur, std::unique_ptr<ExprNode>* out);
  Status ParseMul(TokenCursor* cur, std::unique_ptr<ExprNode>* out);
  Status ParseUnary(TokenCursor* cur, std::unique_ptr<ExprNode>* out);
  Status ParseInOrExists(TokenCursor* cur, std::unique_ptr<ExprNode> lhs,
                         bool not_op, std::unique_ptr<ExprNode>* out);
  std::string ExtractBalancedParenSql(TokenCursor* cur);
};

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
