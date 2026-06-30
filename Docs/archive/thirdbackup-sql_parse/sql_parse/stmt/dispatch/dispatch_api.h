#pragma once

#include <string>
#include <vector>

#include "common/status.h"
#include "concept/query/ast.h"

namespace heterodb::sql_parse::detail {

/** Query path: WITH / SELECT (not used by stmt route handlers; kept for router). */
Status ParseStmtQuery(const std::vector<std::string>& tokens, size_t* pos,
                      SqlStatement* out);

Status ParseStmtMeta(const std::string& head,
                     const std::vector<std::string>& tokens, size_t* pos,
                     SqlStatement* out);

Status ParseStmtDml(const std::string& head,
                    const std::vector<std::string>& tokens, size_t* pos,
                    SqlStatement* out);

Status ParseStmtDdl(const std::string& head,
                    const std::vector<std::string>& tokens, size_t* pos,
                    SqlStatement* out);

Status ParseStmtTxn(const std::string& head,
                    const std::vector<std::string>& tokens, size_t* pos,
                    SqlStatement* out);

Status ParseStmtPriv(const std::string& head,
                     const std::vector<std::string>& tokens, size_t* pos,
                     SqlStatement* out);

Status ParseStmtSetTxn(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out);

Status ParseStmtSet(const std::vector<std::string>& tokens, size_t* pos,
                    SqlStatement* out);

Status ParseStmtRename(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out);

Status ParseStatementFromTokens(const std::vector<std::string>& tokens,
                                SqlStatement* out);

}  // namespace heterodb::sql_parse::detail
