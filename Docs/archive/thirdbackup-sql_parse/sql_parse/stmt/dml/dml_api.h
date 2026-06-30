#pragma once

// ParseConcept — INSERT / UPDATE / DELETE helpers.
// Layer: ParseConcept | Manifest: StmtRule_Insert, StmtRule_Update, StmtRule_Delete, StmtRule_Replace, StmtRule_Truncate

#include <string>
#include <vector>

#include "common/status.h"
#include "concept/query/ast.h"
#include "sql_parse/stmt/parse_common_api.h"

namespace heterodb::sql_parse {
namespace detail {

Status ParseOnDuplicateKeyUpdate(const std::vector<std::string>& tokens, size_t* pos,
                                 SqlStatement* out);
Status ParseInsertSelectSql(const std::vector<std::string>& tokens, size_t* pos,
                            SqlStatement* out);
Status ParseInsertSetClause(const std::vector<std::string>& tokens, size_t* pos,
                            SqlStatement* out);
Status ParseInsertValues(const std::vector<std::string>& tokens, size_t* pos,
                         SqlStatement* out);
Status ParseInsertValues(const std::vector<std::string>& tokens, size_t* pos,
                         SqlStatement* out);
Status ParseSimpleWhere(const std::vector<std::string>& tokens, size_t* pos,
                        SqlStatement* out);

Status ParseStmtDml(const std::string& head, const std::vector<std::string>& tokens,
                    size_t* pos, SqlStatement* out);

}  // namespace detail
}  // namespace heterodb::sql_parse
