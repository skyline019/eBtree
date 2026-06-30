#pragma once

// ParseConcept — shared statement helpers (DB scope, literals, key scan).
// Layer: ParseConcept | Manifest: (internal)

#include <string>
#include <vector>

#include "common/status.h"
#include "concept/query/ast.h"
#include "concept/query/expr.h"
#include "sql_parse/adapters/parse_catalog_bridge.h"

namespace heterodb::sql_parse {
namespace detail {

struct ParseDatabaseScope {
  explicit ParseDatabaseScope(const std::string& db);
  ~ParseDatabaseScope();
  std::string owned;
  const std::string* prev;
};

struct ParseSessionScope {
  explicit ParseSessionScope(const std::string& db,
                             const ParseCatalogBridge* catalog_bridge = nullptr);
  ~ParseSessionScope();
  ParseDatabaseScope db;
  const ParseCatalogBridge* prev_catalog{nullptr};
};

bool ExprTreeHasScalarSubquery(const Expr& expr);
std::string ParseCurrentDatabase();
Status ResolveQualifiedTableToken(const std::string& token, std::string* database,
                                  std::string* table);
std::string Unquote(const std::string& tok);
Status ParseCompareOp(const std::string& op, CompareOp* out);
std::string BareColumnToken(const std::string& col);
void ApplyKeyBound(KeyScanRange* range, CompareOp op, const std::string& val);
void SyncKeyScanRange(SqlStatement* out);

}  // namespace detail
}  // namespace heterodb::sql_parse
