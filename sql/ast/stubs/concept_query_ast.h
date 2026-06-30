#pragma once

// Bridge header for thirdbackup-style includes → ebtree rich AST.
#include "sql/ast/query_ast.h"
#include "sql/ast/expr_ast.h"
#include "sql/ast/select_ast.h"

namespace heterodb {
namespace query {

using SqlStatement = ebtree::sql::QueryStatement;

}  // namespace query
}  // namespace heterodb
