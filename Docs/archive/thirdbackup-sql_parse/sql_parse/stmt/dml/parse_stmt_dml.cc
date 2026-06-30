// ParseConcept | DML statement routing (INSERT/UPDATE/DELETE/TRUNCATE/REPLACE).
#include "sql_parse/stmt/dml/dml_api.h"

#include "common/parse_error.h"
#include "sql_parse/stmt/parse_common_api.h"
#include "sql_parse/stmt/where/where_api.h"
#include "sql_parse/adapters/parse_catalog_bridge.h"
#include "sql_parse/pred/where_parse.h"
#include "sql_parse/shared/parse_shared.h"

#include <sstream>

namespace heterodb::sql_parse {
namespace detail {
Status ParseStmtDml(const std::string& head, const std::vector<std::string>& tokens, size_t* pos, SqlStatement* out)  {
  if (head == "TRUNCATE") {
    if ((*pos) >= tokens.size() || Upper(tokens[*pos]) != "TABLE") {
      return Status::Syntax("TRUNCATE TABLE expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    out->kind = SqlStatementKind::kTruncate;
    return ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->table);
  }

  if (head == "REPLACE") {
    out->kind = SqlStatementKind::kInsert;
    out->replace_into = true;
    out->insert_columns.clear();
    out->insert_values.clear();
    out->insert_row_sets.clear();
    out->insert_is_select = false;
    out->on_duplicate_update_columns.clear();
    out->on_duplicate_update_values.clear();
    return ParseInsertValues(tokens, pos, out);
  }

  if (head == "INSERT") {
    out->kind = SqlStatementKind::kInsert;
    out->replace_into = false;
    out->insert_ignore = false;
    out->insert_set_form = false;
    if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "IGNORE") {
      ++(*pos);
      out->insert_ignore = true;
    }
    out->insert_columns.clear();
    out->insert_values.clear();
    out->insert_row_sets.clear();
    out->insert_is_select = false;
    out->on_duplicate_update_columns.clear();
    out->on_duplicate_update_values.clear();
    return ParseInsertValues(tokens, pos, out);
  }

  if (head == "UPDATE") {
    out->kind = SqlStatementKind::kUpdate;
    Status upd_s = ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->table);
    if (!upd_s.ok()) {
      return upd_s;
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
    while ((*pos) < tokens.size() && IsJoinLeadToken(tokens[*pos])) {
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
    if (Upper(tokens[*pos]) != "SET") {
      return Status::Syntax("expected SET", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if ((*pos) < tokens.size() && tokens[*pos] == "key" && tokens[(*pos) + 1] == "=") {
      (*pos) += 2;
      out->update_key = Unquote(tokens[(*pos)++]);
      if (tokens[*pos] != "," || tokens[(*pos) + 1] != "value" || tokens[(*pos) + 2] != "=") {
        return Status::Syntax("expected , value =", ParseErrorKind::kSyntax);
      }
      (*pos) += 3;
      out->update_value = Unquote(tokens[(*pos)++]);
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "WHERE") {
        Predicate p;
        p.column = "key";
        p.op = CompareOp::kEq;
        ++(*pos);
        if ((*pos) >= tokens.size() || tokens[*pos] != "=") {
          return Status::Syntax("WHERE key =", ParseErrorKind::kSyntax);
        }
        ++(*pos);
        out->update_key = Unquote(tokens[(*pos)++]);
        p.value = out->update_key;
        out->where = p;
      }
      return Status::OK();
    }
    while ((*pos) < tokens.size() && Upper(tokens[*pos]) != "WHERE" &&
           Upper(tokens[*pos]) != "LIMIT") {
      if (!out->update_columns.empty() && tokens[*pos] != ",") {
        return Status::Syntax("expected , between assignments", ParseErrorKind::kSyntax);
      }
      if (tokens[*pos] == ",") {
        ++(*pos);
      }
      const std::string col = tokens[(*pos)++];
      if ((*pos) >= tokens.size() || tokens[*pos] != "=") {
        return Status::Syntax("expected = after column", ParseErrorKind::kSyntax);
      }
      ++(*pos);
      SqlLiteralValue v;
      v.tag = SqlLiteralValue::Tag::kString;
      v.str_val = Unquote(tokens[(*pos)++]);
      out->update_columns.push_back(col);
      out->update_values.push_back(v);
    }
    if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "WHERE") {
      Status ws = pred::ParseWhere(tokens, pos, out);
      if (!ws.ok()) {
        return ws;
      }
    }
    if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "LIMIT") {
      ++(*pos);
      if ((*pos) >= tokens.size()) {
        return Status::Syntax("LIMIT requires value", ParseErrorKind::kSyntax);
      }
      out->limit = static_cast<uint64_t>(std::stoull(tokens[(*pos)++]));
    }
    return Status::OK();
  }

  if (head == "DELETE") {
    out->kind = SqlStatementKind::kDelete;
    std::string delete_target;
    if ((*pos) < tokens.size() && Upper(tokens[*pos]) != "FROM") {
      delete_target = tokens[(*pos)++];
    }
    if ((*pos) >= tokens.size() || Upper(tokens[*pos]) != "FROM") {
      return Status::Syntax("expected FROM", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    Status del_s = ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->table);
    if (!del_s.ok()) {
      return del_s;
    }
    if (!delete_target.empty() && delete_target != out->table) {
      const auto dot = delete_target.find('.');
      const std::string bare =
          dot == std::string::npos ? delete_target : delete_target.substr(dot + 1);
      if (bare != out->table) {
        out->primary_table_alias = bare;
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
    while ((*pos) < tokens.size() && IsJoinLeadToken(tokens[*pos])) {
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
    if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "WHERE") {
      Status s = pred::ParseWhere(tokens, pos, out);
      if (!s.ok()) {
        return s;
      }
      if (out->key_range.has_value()) {
        out->where = Predicate{};
        out->where->column = "key";
        out->where->op = CompareOp::kEq;
        out->where->value = out->key_range->start;
        out->delete_key = out->key_range->start;
      } else if (out->where.has_value()) {
        out->delete_key = out->where->value;
      }
    }
    if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "LIMIT") {
      ++(*pos);
      if ((*pos) >= tokens.size()) {
        return Status::Syntax("LIMIT requires value", ParseErrorKind::kSyntax);
      }
      out->limit = static_cast<uint64_t>(std::stoull(tokens[(*pos)++]));
    }
    return Status::OK();
  }
  return Status::Syntax("not handled by ParseStmtDml", ParseErrorKind::kSyntax);
}

}  // namespace detail
}  // namespace heterodb::sql_parse
