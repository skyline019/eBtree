#pragma once

// ParseConcept — CREATE / ALTER / DROP DDL helpers.
// Layer: ParseConcept | Manifest: StmtRule_Create, StmtRule_Drop, StmtRule_Alter, StmtRule_Reindex

#include <cstdint>
#include <string>
#include <vector>

#include "common/status.h"
#include "concept/catalog/catalog_types.h"
#include "concept/query/ast.h"
#include "sql_parse/stmt/parse_common_api.h"

namespace heterodb::sql_parse {
namespace detail {

Status ParseColumnTypeTok(const std::vector<std::string>& tokens, size_t* pos,
                          ColumnType* type, uint32_t* max_len);
Status ParseCreateView(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out);
Status ParseCreateTable(const std::vector<std::string>& tokens, size_t* pos,
                        SqlStatement* out);

Status ParseStmtDdl(const std::string& head, const std::vector<std::string>& tokens,
                    size_t* pos, SqlStatement* out);

}  // namespace detail
}  // namespace heterodb::sql_parse
