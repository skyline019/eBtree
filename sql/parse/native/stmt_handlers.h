#pragma once

#include "ebtree/common/status.h"
#include "sql/ast/query_ast.h"
#include "sql/parse/core/first_match_registry.h"
#include "sql/parse/core/stmt_classifier.h"
#include "sql/parse/core/parse_context.h"

namespace ebtree {
namespace sql {
namespace parse {

void InstallNativeStmtRules(FirstMatchRegistry* registry);
void InstallDmlRules(FirstMatchRegistry* registry);
void InstallDdlRules(FirstMatchRegistry* registry);
void SyncLegacySelectFields(QueryStatement* out);

Status ParseReindexQuery(const std::string& sql, QueryStatement* out);

Status ParseOpen(ParseContext* ctx);
Status ParseCreateTable(ParseContext* ctx);
Status ParseCreateIndex(ParseContext* ctx);
Status ParseDropIndex(ParseContext* ctx);
Status ParseInsert(ParseContext* ctx);
Status ParseUpdate(ParseContext* ctx);
Status ParseDelete(ParseContext* ctx);
Status ParseDropTable(ParseContext* ctx);
Status ParseAlterTable(ParseContext* ctx);
Status ParseSelectStmt(ParseContext* ctx);
Status ParseParseOnly(ParseContext* ctx, StmtClass cls);

Status ParsePrepareQuery(const std::string& sql, QueryStatement* out);
Status ParseExecuteQuery(const std::string& sql, QueryStatement* out);
Status ParseCreateViewQuery(const std::string& sql, QueryStatement* out);
Status ParseDropViewQuery(const std::string& sql, QueryStatement* out);
Status ParseCreateTriggerQuery(const std::string& sql, QueryStatement* out);
Status ParseDropTriggerQuery(const std::string& sql, QueryStatement* out);

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
