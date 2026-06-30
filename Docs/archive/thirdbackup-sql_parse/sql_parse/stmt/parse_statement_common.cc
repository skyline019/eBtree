#include "sql_parse/stmt/parse_common_api.h"

#include "common/parse_error.h"

#include "concept/query/parser.h"
#include "concept/query/expr.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "concept/catalog/catalog_codec.h"
#include "concept/catalog/value_codec.h"
#include "concept/schema/schema.h"
#include "sql_parse/adapters/parse_catalog_bridge.h"
#include "sql_parse/core/parse_context.h"
#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {

using heterodb::MarkCorrelatedRefs;
using heterodb::sql_parse::NormalizeSqlInput;
using heterodb::sql_parse::SplitTokens;
using heterodb::sql_parse::SqlInputNeedsNormalization;
using heterodb::sql_parse::Trim;
using heterodb::sql_parse::Upper;

bool ExprTreeHasScalarSubquery(const Expr& expr) {
  if (expr.kind == ExprKind::kScalarSubquery) {
    return true;
  }
  if (expr.unary_operand != nullptr &&
      ExprTreeHasScalarSubquery(*expr.unary_operand)) {
    return true;
  }
  if (expr.binary_left != nullptr &&
      ExprTreeHasScalarSubquery(*expr.binary_left)) {
    return true;
  }
  if (expr.binary_right != nullptr &&
      ExprTreeHasScalarSubquery(*expr.binary_right)) {
    return true;
  }
  for (const auto* child : expr.case_when_conds) {
    if (child != nullptr && ExprTreeHasScalarSubquery(*child)) {
      return true;
    }
  }
  for (const auto* child : expr.case_then_vals) {
    if (child != nullptr && ExprTreeHasScalarSubquery(*child)) {
      return true;
    }
  }
  if (expr.case_else != nullptr &&
      ExprTreeHasScalarSubquery(*expr.case_else)) {
    return true;
  }
  if (expr.cast_operand != nullptr &&
      ExprTreeHasScalarSubquery(*expr.cast_operand)) {
    return true;
  }
  for (const auto* child : expr.func_args) {
    if (child != nullptr && ExprTreeHasScalarSubquery(*child)) {
      return true;
    }
  }
  return false;
}

thread_local const std::string* g_parse_current_database = nullptr;
thread_local const ParseCatalogBridge* g_parse_catalog_bridge = nullptr;

ParseDatabaseScope::ParseDatabaseScope(const std::string& db)
    : owned(db), prev(g_parse_current_database) {
  g_parse_current_database = &owned;
}

ParseDatabaseScope::~ParseDatabaseScope() {
  g_parse_current_database = prev;
}

ParseSessionScope::ParseSessionScope(const std::string& db,
                                     const ParseCatalogBridge* catalog_bridge)
    : db(db), prev_catalog(g_parse_catalog_bridge) {
  g_parse_catalog_bridge =
      catalog_bridge != nullptr ? catalog_bridge : &DefaultCatalogBridge();
}

ParseSessionScope::~ParseSessionScope() {
  g_parse_catalog_bridge = prev_catalog;
}

std::string ParseCurrentDatabase() {
  return g_parse_current_database != nullptr ? *g_parse_current_database
                                           : std::string(kDefaultDatabaseName);
}

Status ResolveQualifiedTableToken(const std::string& token, std::string* database,
                                  std::string* table) {
  const ParseCatalogBridge& bridge =
      g_parse_catalog_bridge != nullptr ? *g_parse_catalog_bridge
                                        : DefaultCatalogBridge();
  return bridge.ResolveQualifiedTable(token, ParseCurrentDatabase(), database,
                                      table);
}

using heterodb::ParseLiteral;
using heterodb::SqlLiteralToString;

std::string Unquote(const std::string& tok) {
  if (tok.size() >= 2 &&
      ((tok.front() == '\'' && tok.back() == '\'') ||
       (tok.front() == '"' && tok.back() == '"'))) {
    return tok.substr(1, tok.size() - 2);
  }
  return tok;
}

Status ParseCompareOp(const std::string& op, CompareOp* out) {
  if (op == "=") {
    *out = CompareOp::kEq;
  } else if (op == "<") {
    *out = CompareOp::kLt;
  } else if (op == "<=") {
    *out = CompareOp::kLe;
  } else if (op == ">") {
    *out = CompareOp::kGt;
  } else if (op == ">=") {
    *out = CompareOp::kGe;
  } else if (op == "!=" || op == "<>") {
    *out = CompareOp::kNe;
  } else {
    return Status::Syntax("unsupported comparison operator: " + op, ParseErrorKind::kSyntax);
  }
  return Status::OK();
}

std::string BareColumnToken(const std::string& col) {
  const size_t dot = col.find('.');
  return dot == std::string::npos ? col : col.substr(dot + 1);
}

bool KeyPredicateNarrowsScanRange(const ColumnPredicate& cp) {
  if (BareColumnToken(cp.column) != "key") {
    return false;
  }
  if (!cp.value2.empty()) {
    return true;
  }
  switch (cp.op) {
    case CompareOp::kEq:
    case CompareOp::kGe:
    case CompareOp::kGt:
    case CompareOp::kLe:
    case CompareOp::kLt:
      return true;
    default:
      return false;
  }
}

void ApplyKeyBound(KeyScanRange* range, CompareOp op, const std::string& val) {
  switch (op) {
    case CompareOp::kEq:
      range->start = val;
      range->end_exclusive = val + '\x00';
      break;
    case CompareOp::kGe:
      if (val > range->start) {
        range->start = val;
      }
      break;
    case CompareOp::kGt:
      if (val + '\x00' > range->start) {
        range->start = val + '\x00';
      }
      break;
    case CompareOp::kLe:
      if (val + '\x00' < range->end_exclusive) {
        range->end_exclusive = val + '\x00';
      }
      break;
    case CompareOp::kLt:
      if (val < range->end_exclusive) {
        range->end_exclusive = val;
      }
      break;
    default:
      break;
  }
}

void SyncKeyScanRange(SqlStatement* out) {
  if (out == nullptr) {
    return;
  }
  KeyScanRange range;
  range.start.clear();
  range.end_exclusive = "\xFF";
  bool any_key = false;
  const auto apply = [&](const std::vector<ColumnPredicate>& preds) {
    for (const auto& cp : preds) {
      if (!KeyPredicateNarrowsScanRange(cp)) {
        continue;
      }
      any_key = true;
      if (!cp.value2.empty()) {
        ApplyKeyBound(&range, CompareOp::kGe, cp.value);
        ApplyKeyBound(&range, CompareOp::kLe, cp.value2);
      } else {
        ApplyKeyBound(&range, cp.op, cp.value);
      }
    }
  };
  apply(out->where_and);
  for (const auto& clause : out->where_or) {
    apply(clause);
  }
  if (any_key) {
    out->key_range = range;
  }
}


}  // namespace detail
}  // namespace heterodb::sql_parse
