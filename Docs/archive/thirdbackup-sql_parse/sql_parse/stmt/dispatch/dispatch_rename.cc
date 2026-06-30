#include "sql_parse/stmt/dispatch/dispatch_api.h"

#include "common/parse_error.h"
#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {

Status ParseStmtRename(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out) {
  if (pos == nullptr || out == nullptr) {
    return Status::InvalidArgument("null argument");
  }
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "TABLE") {
    return Status::Syntax("RENAME TABLE expected", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if (*pos >= tokens.size()) {
    return Status::Syntax("RENAME TABLE needs old name", ParseErrorKind::kSyntax);
  }
  out->kind = SqlStatementKind::kAlterTable;
  out->alter_kind = AlterKind::kRenameTable;
  out->table = tokens[(*pos)++];
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "TO") {
    return Status::Syntax("RENAME TABLE needs TO", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if (*pos >= tokens.size()) {
    return Status::Syntax("RENAME TABLE needs new name", ParseErrorKind::kSyntax);
  }
  out->rename_table_to = tokens[(*pos)++];
  return Status::OK();
}

}  // namespace detail
}  // namespace heterodb::sql_parse
