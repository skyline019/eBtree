#pragma once

// ParseConcept — WHERE predicates, JOIN, WINDOW (implementation in join_window_parse.cc).
// WHERE entry: pred::ParseWhere (pred/where_parse.cc).
// Layer: ParseConcept | Manifest: PredWhereClause

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/status.h"
#include "concept/catalog/catalog_types.h"
#include "concept/query/ast.h"
#include "concept/query/logical_plan.h"
#include "sql_parse/stmt/parse_common_api.h"

namespace heterodb::sql_parse {
namespace detail {

Status ParseColumnPredicate(const std::vector<std::string>& tokens, size_t* pos,
                            KeyScanRange* range, SqlStatement* out);
Status ParseKeyPredicate(const std::vector<std::string>& tokens, size_t* pos,
                         KeyScanRange* range, SqlStatement* out);
bool IsWhereStopToken(const std::string& tok);
bool IsJoinLeadToken(const std::string& tok);
Status ParseFkReferentialAction(const std::vector<std::string>& tokens, size_t* pos,
                                FkOnDelete* out);
Status ParseFkOnDeleteClause(const std::vector<std::string>& tokens, size_t* pos,
                             FkOnDelete* out);
Status ParseFkOnUpdateClause(const std::vector<std::string>& tokens, size_t* pos,
                             FkOnUpdate* out);
bool IsReservedTableAlias(const std::string& tok);
bool IsTableAliasStopToken(const std::string& tok);
Status ExtractParenthesizedSelectSql(const std::vector<std::string>& tokens,
                                     size_t* pos, std::string* sql_out);
Status ParseRequiredDerivedAlias(const std::vector<std::string>& tokens, size_t* pos,
                                 std::string* alias_out);
Status ParseOptionalTableAlias(const std::vector<std::string>& tokens, size_t* pos,
                               const std::string& base_table, std::string* outer_name,
                               std::unordered_map<std::string, std::string>* aliases);
Status ParseJoinUsingColumns(const std::vector<std::string>& tokens, size_t* pos,
                             JoinClause* join);
Status ParseJoinOnColumns(const std::vector<std::string>& tokens, size_t* pos,
                          JoinClause* join);
Status ParseJoinPostTable(const std::vector<std::string>& tokens, size_t* pos,
                          JoinClause* join);
Status ParseOneJoinClause(const std::vector<std::string>& tokens, size_t* pos,
                          JoinClause* join, SqlStatement* out);
std::string AggregateOutputColumn(AggregateFn fn);
std::string WindowOutputColumn(WindowFn fn);
Status ParseWindowFrameBound(const std::vector<std::string>& tokens, size_t* pos,
                             WindowFrameBound* bound);
Status ParseWindowFrame(const std::vector<std::string>& tokens, size_t* pos,
                        WindowFrame* frame);
Status ApplyNamedWindowDef(const std::unordered_map<std::string, WindowExpr>& defs,
                           const std::string& name, WindowExpr* we);
Status ParseWindowOverBody(const std::vector<std::string>& tokens, size_t* pos,
                           WindowExpr* we);
Status ParseRecursiveCte(const std::vector<std::string>& tokens, size_t* pos,
                         SqlStatement* out);
Status ParseWherePrimary(const std::vector<std::string>& tokens, size_t* pos,
                         KeyScanRange* range, std::vector<ColumnPredicate>* clause,
                         SqlStatement* scratch);
Status ParseWhereAndTerm(const std::vector<std::string>& tokens, size_t* pos,
                         KeyScanRange* range, std::vector<ColumnPredicate>* clause,
                         SqlStatement* scratch);
Status ParseWhereExpr(const std::vector<std::string>& tokens, size_t* pos,
                      KeyScanRange* range, SqlStatement* out);
Status ParseWhereClause(const std::vector<std::string>& tokens, size_t* pos,
                        SqlStatement* out);

}  // namespace detail
}  // namespace heterodb::sql_parse
