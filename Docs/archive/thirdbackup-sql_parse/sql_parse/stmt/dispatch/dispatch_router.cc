#include "common/parse_error.h"
#include "sql_parse/stmt/dispatch/dispatch_api.h"
#include "sql_parse/stmt/select/select_api.h"
#include "sql_parse/stmt/stmt_head_table.h"
#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {

Status ParseStmtQuery(const std::vector<std::string>& tokens, size_t* pos,
                      SqlStatement* out) {
  if (*pos > 0 && Upper(tokens[0]) == "WITH") {
    return ParseWithClause(tokens, pos, out);
  }
  if (*pos > 0 && Upper(tokens[0]) == "SELECT") {
    return ParseQueryExpression(tokens, pos, out);
  }
  return Status::Syntax("expected WITH or SELECT", ParseErrorKind::kSyntax);
}

Status ParseStatementFromTokens(const std::vector<std::string>& tokens,
                                SqlStatement* out) {
  if (out == nullptr) {
    return Status::InvalidArgument("null output");
  }
  if (tokens.empty()) {
    return Status::Syntax("empty sql", ParseErrorKind::kSyntax);
  }
  const std::string head = Upper(tokens[0]);
  size_t pos = 1;
  const StmtHeadRoute* route = LookupStmtHeadRoute(head);
  if (route == nullptr) {
    return Status::Syntax("unsupported statement: " + head,
                          ParseErrorKind::kUnsupportedStatement);
  }
  switch (route->kind) {
    case StmtHeadRouteKind::kQuery:
      return ParseStmtQuery(tokens, &pos, out);
    case StmtHeadRouteKind::kMeta:
      return ParseStmtMeta(head, tokens, &pos, out);
    case StmtHeadRouteKind::kPriv:
      return ParseStmtPriv(head, tokens, &pos, out);
    case StmtHeadRouteKind::kDml:
      return ParseStmtDml(head, tokens, &pos, out);
    case StmtHeadRouteKind::kRename:
      return ParseStmtRename(tokens, &pos, out);
    case StmtHeadRouteKind::kDdl:
      return ParseStmtDdl(head, tokens, &pos, out);
    case StmtHeadRouteKind::kSet:
      return ParseStmtSet(tokens, &pos, out);
    case StmtHeadRouteKind::kTxn:
      return ParseStmtTxn(head, tokens, &pos, out);
    case StmtHeadRouteKind::kAnalyze:
      break;
    case StmtHeadRouteKind::kOptimize:
      break;
    case StmtHeadRouteKind::kFlush:
      break;
    default:
      return Status::Syntax("unsupported statement: " + head,
                            ParseErrorKind::kUnsupportedStatement);
  }
  if (head == "ANALYZE") {
    if (pos >= tokens.size() || Upper(tokens[pos]) != "TABLE") {
      return Status::Syntax("ANALYZE TABLE expected", ParseErrorKind::kSyntax);
    }
    ++pos;
    if (pos >= tokens.size()) {
      return Status::Syntax("ANALYZE TABLE needs table name", ParseErrorKind::kSyntax);
    }
    out->kind = SqlStatementKind::kAnalyzeTable;
    out->admin_table_names.push_back(tokens[pos++]);
    while (pos < tokens.size() && tokens[pos] == ",") {
      ++pos;
      if (pos >= tokens.size()) {
        break;
      }
      out->admin_table_names.push_back(tokens[pos++]);
    }
    return Status::OK();
  }
  if (head == "OPTIMIZE") {
    if (pos >= tokens.size() || Upper(tokens[pos]) != "TABLE") {
      return Status::Syntax("OPTIMIZE TABLE expected", ParseErrorKind::kSyntax);
    }
    ++pos;
    if (pos >= tokens.size()) {
      return Status::Syntax("OPTIMIZE TABLE needs table name", ParseErrorKind::kSyntax);
    }
    out->kind = SqlStatementKind::kOptimizeTable;
    out->admin_table_names.push_back(tokens[pos++]);
    return Status::OK();
  }
  if (head == "FLUSH") {
    if (pos >= tokens.size() || Upper(tokens[pos]) != "TABLES") {
      return Status::Syntax("FLUSH TABLES expected", ParseErrorKind::kSyntax);
    }
    ++pos;
    out->kind = SqlStatementKind::kFlushTables;
    return Status::OK();
  }
  return Status::Syntax("unsupported statement: " + head,
                        ParseErrorKind::kUnsupportedStatement);
}

}  // namespace detail
}  // namespace heterodb::sql_parse
