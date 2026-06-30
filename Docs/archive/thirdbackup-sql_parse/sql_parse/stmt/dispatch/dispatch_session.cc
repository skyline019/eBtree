#include "common/parse_error.h"
#include "sql_parse/stmt/dispatch/dispatch_api.h"
#include "sql_parse/stmt/parse_common_api.h"
#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {

namespace {

bool IsAllowedSessionVar(const std::string& upper_name) {
  return upper_name == "AUTOCOMMIT" || upper_name == "NAMES" ||
         upper_name == "CHARACTER_SET_CLIENT" ||
         upper_name == "CHARACTER_SET_RESULTS" ||
         upper_name == "CHARACTER_SET_CONNECTION";
}

std::string SessionVarStorageName(const std::string& upper_name) {
  if (upper_name == "AUTOCOMMIT") {
    return "autocommit";
  }
  if (upper_name == "NAMES") {
    return "names";
  }
  if (upper_name == "CHARACTER_SET_CLIENT") {
    return "character_set_client";
  }
  if (upper_name == "CHARACTER_SET_RESULTS") {
    return "character_set_results";
  }
  if (upper_name == "CHARACTER_SET_CONNECTION") {
    return "character_set_connection";
  }
  return upper_name;
}

}  // namespace

Status ParseStmtSet(const std::vector<std::string>& tokens, size_t* pos,
                    SqlStatement* out) {
  if (pos == nullptr || out == nullptr) {
    return Status::InvalidArgument("null argument");
  }
  if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "TRANSACTION") {
    return ParseStmtSetTxn(tokens, pos, out);
  }
  if ((*pos) >= tokens.size()) {
    return Status::Syntax("SET requires variable name", ParseErrorKind::kSyntax);
  }
  std::string var = tokens[(*pos)++];
  if (!var.empty() && var[0] == '@') {
    return Status::Syntax("unsupported SET session variable",
                          ParseErrorKind::kUnknownSetVariable);
  }
  const std::string upper_var = Upper(var);
  if (!IsAllowedSessionVar(upper_var)) {
    return Status::Syntax("unsupported SET session variable: " + var,
                        ParseErrorKind::kUnknownSetVariable);
  }
  if (upper_var == "NAMES" && (*pos) < tokens.size() && tokens[*pos] != "=") {
    out->kind = SqlStatementKind::kSetSession;
    out->session_var_name = SessionVarStorageName(upper_var);
    out->session_var_value = Unquote(tokens[(*pos)++]);
    return Status::OK();
  }
  if ((*pos) >= tokens.size() || tokens[*pos] != "=") {
    return Status::Syntax("expected = in SET", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if ((*pos) >= tokens.size()) {
    return Status::Syntax("SET requires value", ParseErrorKind::kSyntax);
  }
  out->kind = SqlStatementKind::kSetSession;
  out->session_var_name = SessionVarStorageName(upper_var);
  out->session_var_value = Unquote(tokens[(*pos)++]);
  return Status::OK();
}

}  // namespace detail
}  // namespace heterodb::sql_parse
