#include "sql_parse/stmt/dispatch/dispatch_api.h"

#include "common/parse_error.h"
#include "sql_parse/stmt/parse_common_api.h"
#include "concept/security/privilege_catalog.h"
#include "sql_parse/adapters/parse_catalog_bridge.h"
#include "sql_parse/shared/parse_shared.h"

#include <sstream>

namespace heterodb::sql_parse {
namespace detail {
Status ParseStmtPriv(const std::string& head, const std::vector<std::string>& tokens, size_t* pos, SqlStatement* out)  {
  if (head == "GRANT" || head == "REVOKE") {
    out->kind = head == "GRANT" ? SqlStatementKind::kGrant : SqlStatementKind::kRevoke;
    if ((*pos) >= tokens.size()) {
      return Status::Syntax("privilege expected", ParseErrorKind::kSyntax);
    }
    uint32_t mask = 0;
    while ((*pos) < tokens.size() && Upper(tokens[*pos]) != "ON") {
      uint32_t bit = 0;
      Status ps = ParsePrivilegeMask(tokens[(*pos)++], &bit);
      if (!ps.ok()) {
        return ps;
      }
      mask |= bit;
      if ((*pos) < tokens.size() && tokens[*pos] == ",") {
        ++(*pos);
      }
    }
    out->grant_privilege_mask = mask;
    if ((*pos) >= tokens.size() || Upper(tokens[*pos]) != "ON") {
      return Status::Syntax("ON expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    Status gs = ResolveQualifiedTableToken(tokens[(*pos)++], &out->grant_database,
                                           &out->grant_table);
    if (!gs.ok()) {
      return gs;
    }
    const bool is_revoke = out->kind == SqlStatementKind::kRevoke;
    const std::string grant_kw = is_revoke ? "FROM" : "TO";
    if ((*pos) >= tokens.size() || Upper(tokens[*pos]) != grant_kw) {
      return Status::Syntax(grant_kw + " expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if ((*pos) >= tokens.size()) {
      return Status::Syntax("grantee expected", ParseErrorKind::kSyntax);
    }
    out->grantee = tokens[(*pos)++];
    return Status::OK();
  }
  return Status::Syntax("not handled by ParseStmtPriv", ParseErrorKind::kSyntax);
}

}  // namespace detail
}  // namespace heterodb::sql_parse
