#pragma once

// ParseConcept — SELECT / WITH / set operations.
// Layer: ParseConcept | Manifest: StmtRule_Select, StmtRule_With

#include <string>
#include <vector>

#include "common/status.h"
#include "concept/query/ast.h"
#include "sql_parse/stmt/parse_common_api.h"

namespace heterodb::sql_parse {
namespace detail {

bool IsSetOpLeadToken(const std::string& tok);
Status ParseSetOpKindToken(const std::string& tok, SetOpKind* kind);
Status ParseSelectCore(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out);
Status ParseSelectTail(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out);
Status ParseQueryExpression(const std::vector<std::string>& tokens, size_t* pos,
                            SqlStatement* out);
Status ParseOneCte(const std::vector<std::string>& tokens, size_t* pos,
                   SqlStatement* out);
Status ParseWithClause(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out);
Status ParseSelectStatement(const std::vector<std::string>& tokens, size_t* pos,
                            SqlStatement* out);
Status ParseSelectProjectList(const std::vector<std::string>& tokens, size_t* pos,
                              SqlStatement* out);
Status ParseSelectFromJoinWhere(const std::vector<std::string>& tokens, size_t* pos,
                                SqlStatement* out);
Status ParseSelectGroupHaving(const std::vector<std::string>& tokens, size_t* pos,
                              SqlStatement* out);

}  // namespace detail
}  // namespace heterodb::sql_parse
