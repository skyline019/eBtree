#pragma once

#include "concept/query/ast.h"

namespace heterodb::sql_parse::pred {

/**
 * WHERE 双轨契约：where_expr 与 where_and/where_or/key_range 互斥。
 * 主路径为列谓词 + OR 组；表达式兜底经 WhereLooksExprHeavy → ParseExpr。
 * 含标量子查询的 Expr 树不得进入 where_expr（见 where_parse.cc）。
 */
void ApplyWhereNormalize(SqlStatement* stmt);

void ApplyHavingNormalize(SqlStatement* stmt);

}  // namespace heterodb::sql_parse::pred
