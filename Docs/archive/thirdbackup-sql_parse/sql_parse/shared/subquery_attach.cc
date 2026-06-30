#include "sql_parse/shared/subquery_attach.h"

#include "common/parse_error.h"
#include "sql_parse/stmt/parse_statement.h"

namespace heterodb::sql_parse {

namespace {

std::string TokensToSql(const std::vector<std::string>& tokens, size_t start,
                        size_t end) {
  std::string out;
  for (size_t i = start; i < end; ++i) {
    if (i > start) {
      out.push_back(' ');
    }
    out += tokens[i];
  }
  return out;
}

}  // namespace

Status AttachSelectSubqueryFromTokens(const std::vector<std::string>& tokens,
                                      size_t start, size_t end,
                                      std::shared_ptr<SqlStatement>* out_stmt) {
  if (out_stmt == nullptr) {
    return Status::InvalidArgument("null argument");
  }
  if (start >= end || end > tokens.size()) {
    return Status::Syntax("empty subquery slice", ParseErrorKind::kEmptySubquery);
  }
  auto stmt = std::make_shared<SqlStatement>();
  const Status ps = ParseSqlStatement(TokensToSql(tokens, start, end), stmt.get());
  if (!ps.ok()) {
    return ps;
  }
  if (stmt->kind != SqlStatementKind::kSelect) {
    return Status::Syntax("subquery must be SELECT", ParseErrorKind::kSyntax);
  }
  *out_stmt = std::move(stmt);
  return Status::OK();
}

void AttachSubqueryToPredicate(const std::shared_ptr<SqlStatement>& stmt,
                             ColumnPredicate* cp) {
  if (cp == nullptr) {
    return;
  }
  cp->subquery_stmt = stmt;
}

void AttachSubqueryToJoin(const std::shared_ptr<SqlStatement>& stmt,
                          JoinClause* join) {
  if (join == nullptr) {
    return;
  }
  join->subquery_stmt = stmt;
}

}  // namespace heterodb::sql_parse
