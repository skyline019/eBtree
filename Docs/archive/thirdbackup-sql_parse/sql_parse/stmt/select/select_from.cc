// ParseConcept | SELECT — FROM / JOIN / WINDOW / WHERE.
#include "sql_parse/stmt/select/select_api.h"

#include "common/parse_error.h"
#include "sql_parse/stmt/where/where_api.h"
#include "sql_parse/stmt/parse_common_api.h"

#include "concept/query/parser.h"
#include "concept/query/expr.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "concept/catalog/catalog_codec.h"
#include "concept/catalog/qualified_name.h"
#include "concept/catalog/value_codec.h"
#include "concept/schema/schema.h"
#include "sql_parse/expr/expr_parse.h"
#include "sql_parse/pred/having_parse.h"
#include "sql_parse/pred/where_parse.h"
#include "sql_parse/shared/parse_shared.h"
#include "sql_parse/stmt/parse_statement.h"

namespace heterodb::sql_parse {
namespace detail {
Status ParseSelectFromJoinWhere(const std::vector<std::string>& tokens, size_t* pos,
                                SqlStatement* out) {
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "FROM") {
    return Status::Syntax("expected FROM", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if (*pos < tokens.size() && tokens[*pos] == "(") {
    std::string sql;
    Status sq = ExtractParenthesizedSelectSql(tokens, pos, &sql);
    if (!sq.ok()) {
      return sq;
    }
    std::string alias;
    Status as = ParseRequiredDerivedAlias(tokens, pos, &alias);
    if (!as.ok()) {
      return as;
    }
    out->table = alias;
    auto derived = std::make_shared<SqlStatement>();
    const Status ds = ParseSqlStatement(sql, derived.get());
    if (!ds.ok()) {
      return ds;
    }
    out->derived_table_queries[alias] = std::move(derived);
    out->catalog_table = false;
    out->primary_table_alias = alias;
    out->outer_table_names.push_back(alias);
    out->table_aliases[alias] = alias;
  } else {
    Status from_s =
        ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->table);
    if (!from_s.ok()) {
      return from_s;
    }
    std::string db_lower = out->database;
    for (char& c : db_lower) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const std::string tbl_upper = Upper(out->table);
    if (db_lower == "information_schema") {
      if (tbl_upper == "SCHEMATA") {
        out->catalog_meta_scan = CatalogMetaScan::kInformationSchemaSchemata;
      } else if (tbl_upper == "TABLES") {
        out->catalog_meta_scan = CatalogMetaScan::kInformationSchemaTables;
      } else if (tbl_upper == "COLUMNS") {
        out->catalog_meta_scan = CatalogMetaScan::kInformationSchemaColumns;
      } else if (tbl_upper == "STATISTICS") {
        out->catalog_meta_scan = CatalogMetaScan::kInformationSchemaStatistics;
      } else if (tbl_upper == "KEY_COLUMN_USAGE") {
        out->catalog_meta_scan =
            CatalogMetaScan::kInformationSchemaKeyColumnUsage;
      } else {
        return Status::Syntax("unsupported information_schema table: " + tbl_upper,
                              ParseErrorKind::kSyntax);
      }
    }
    const std::string base_table = out->table;
    std::string outer_name = base_table;
    Status alias_s =
        ParseOptionalTableAlias(tokens, pos, base_table, &outer_name,
                                &out->table_aliases);
    if (!alias_s.ok()) {
      return alias_s;
    }
    if (outer_name != base_table) {
      out->primary_table_alias = outer_name;
    }
    out->outer_table_names.push_back(outer_name);
  }
  while (*pos < tokens.size() && IsJoinLeadToken(tokens[*pos])) {
    JoinClause jc;
    Status js = ParseOneJoinClause(tokens, pos, &jc, out);
    if (!js.ok()) {
      return js;
    }
    out->joins.push_back(std::move(jc));
  }
  out->has_join = !out->joins.empty();
  if (out->has_join) {
    out->join = out->joins.back();
  }
  while (*pos < tokens.size() && Upper(tokens[*pos]) == "WINDOW") {
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("WINDOW needs name", ParseErrorKind::kSyntax);
    }
    const std::string wname = tokens[(*pos)++];
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "AS") {
      return Status::Syntax("WINDOW name AS expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size() || tokens[*pos] != "(") {
      return Status::Syntax("WINDOW AS ( expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    WindowExpr def;
    Status ws = ParseWindowOverBody(tokens, pos, &def);
    if (!ws.ok()) {
      return ws;
    }
    out->window_defs[wname] = def;
  }
  for (auto& we : out->window_exprs) {
    if (we.named_window_ref.empty()) {
      continue;
    }
    Status nw = ApplyNamedWindowDef(out->window_defs, we.named_window_ref, &we);
    if (!nw.ok()) {
      return nw;
    }
    we.named_window_ref.clear();
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "WHERE") {
    Status s = pred::ParseWhere(tokens, pos, out);
    if (!s.ok()) {
      return s;
    }
    SyncKeyScanRange(out);
  }
  return Status::OK();
}


}  // namespace detail
}  // namespace heterodb::sql_parse
