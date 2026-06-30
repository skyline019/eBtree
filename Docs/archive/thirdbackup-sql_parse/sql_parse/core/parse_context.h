#pragma once

#include <string>
#include <vector>

#include "concept/query/ast.h"
#include "concept/query/expr.h"
#include "sql_parse/adapters/parse_catalog_bridge.h"
#include "sql_parse/core/parse_diagnostic.h"
#include "sql_parse/core/token_cursor.h"

namespace heterodb::sql_parse {

/** Session-level parse state: lex output + statement AST target. */
struct ParseContext {
  std::string raw_sql;
  std::string normalized_sql;
  std::string current_database;
  TokenCursor cursor;
  SqlStatement* out{nullptr};
  /** When set, ExprPlugin_PrattCore may parse into *expr_target. */
  Expr** expr_target{nullptr};
  std::vector<std::string> expr_stop_tokens;
  DiagnosticSink diagnostics;
  /** Optional catalog bridge override (nullptr => DefaultCatalogBridge). */
  const ParseCatalogBridge* catalog_bridge{nullptr};

  const ParseCatalogBridge& Catalog() const {
    return catalog_bridge != nullptr ? *catalog_bridge : DefaultCatalogBridge();
  }

  const std::vector<std::string>& tokens() const { return cursor.tokens(); }
  size_t pos() const { return cursor.pos(); }
  bool AtEnd() const { return cursor.AtEnd(); }
  std::string HeadUpper() const { return cursor.HeadUpper(); }

  void EmitDiagnostic(const std::string& rule, const std::string& message) {
    diagnostics.Add(
        ParseDiagnostic{rule, cursor.pos(), message});
  }
};

}  // namespace heterodb::sql_parse
