#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/status.h"
#include "concept/query/ast.h"

namespace heterodb::sql_parse {

/// Parse token slice `[start,end)` as a SELECT subquery AST (allows SELECT without FROM).
Status AttachSelectSubqueryFromTokens(const std::vector<std::string>& tokens,
                                      size_t start, size_t end,
                                      std::shared_ptr<SqlStatement>* out_stmt);

void AttachSubqueryToPredicate(const std::shared_ptr<SqlStatement>& stmt,
                               ColumnPredicate* cp);

void AttachSubqueryToJoin(const std::shared_ptr<SqlStatement>& stmt, JoinClause* join);

}  // namespace heterodb::sql_parse
