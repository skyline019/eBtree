#include "sql_parse/stmt/dml/dml_api.h"

#include "common/parse_error.h"
#include "sql_parse/stmt/parse_common_api.h"

#include "concept/query/parser.h"
#include "concept/query/expr.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "concept/catalog/catalog_codec.h"
#include "concept/catalog/qualified_name.h"
#include "concept/catalog/value_codec.h"
#include "concept/schema/schema.h"
#include "sql_parse/core/parse_context.h"
#include "sql_parse/shared/parse_shared.h"
#include "sql_parse/stmt/parse_statement.h"

namespace heterodb::sql_parse {
namespace detail {
namespace {

Status ParseInsertValueLiteral(const std::vector<std::string>& tokens, size_t* pos,
                               SqlLiteralValue* out) {
  if (*pos >= tokens.size()) {
    return Status::Syntax("missing insert value", ParseErrorKind::kSyntax);
  }
  std::string lit = tokens[*pos];
  if ((lit == "-" || lit == "+") && *pos + 1 < tokens.size()) {
    const std::string& next = tokens[*pos + 1];
    if (!next.empty() && (std::isdigit(static_cast<unsigned char>(next[0])) ||
                          next[0] == '.')) {
      lit += next;
      *pos += 2;
      return ParseLiteral(lit, ColumnType::kVarchar, out);
    }
  }
  ++(*pos);
  return ParseLiteral(lit, ColumnType::kVarchar, out);
}

}  // namespace

Status ParseOnDuplicateKeyUpdate(const std::vector<std::string>& tokens,
                                 size_t* pos, SqlStatement* out) {
  if (*pos + 4 >= tokens.size() || Upper(tokens[*pos]) != "ON" ||
      Upper(tokens[*pos + 1]) != "DUPLICATE" || Upper(tokens[*pos + 2]) != "KEY" ||
      Upper(tokens[*pos + 3]) != "UPDATE") {
    return Status::OK();
  }
  *pos += 4;
  while (*pos < tokens.size()) {
    if (!out->on_duplicate_update_columns.empty() && tokens[*pos] != ",") {
      break;
    }
    if (tokens[*pos] == ",") {
      ++(*pos);
    }
    if (*pos >= tokens.size()) {
      return Status::Syntax("ON DUPLICATE KEY UPDATE needs assignment", ParseErrorKind::kSyntax);
    }
    const std::string col = tokens[(*pos)++];
    if (*pos >= tokens.size() || tokens[*pos] != "=") {
      return Status::Syntax("expected = in ON DUPLICATE KEY UPDATE", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("ON DUPLICATE KEY UPDATE needs value", ParseErrorKind::kSyntax);
    }
  if (*pos + 2 < tokens.size() && Upper(tokens[*pos]) == "VALUES" &&
      tokens[*pos + 1] == "(") {
    const std::string ref_col = tokens[*pos + 2];
    if (ref_col.back() == ')') {
      std::string bare = ref_col.substr(0, ref_col.size() - 1);
      SqlLiteralValue v;
      v.tag = SqlLiteralValue::Tag::kString;
      if (!out->insert_columns.empty()) {
        bool found = false;
        for (size_t ci = 0; ci < out->insert_columns.size(); ++ci) {
          if (out->insert_columns[ci] == bare ||
              BareColumnToken(out->insert_columns[ci]) == bare) {
            if (ci < out->insert_values.size()) {
              v = out->insert_values[ci];
            }
            found = true;
            break;
          }
        }
        if (!found) {
          return Status::Syntax("VALUES() references unknown insert column", ParseErrorKind::kSyntax);
        }
      }
      out->on_duplicate_update_columns.push_back(col);
      out->on_duplicate_update_values.push_back(v);
      *pos += 3;
      continue;
    }
  }
    SqlLiteralValue v;
    v.tag = SqlLiteralValue::Tag::kString;
    v.str_val = Unquote(tokens[(*pos)++]);
    out->on_duplicate_update_columns.push_back(col);
    out->on_duplicate_update_values.push_back(v);
  }
  return Status::OK();
}

Status ParseInsertSelectSql(const std::vector<std::string>& tokens, size_t* pos,
                            SqlStatement* out) {
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "SELECT") {
    return Status::Syntax("SELECT expected", ParseErrorKind::kSyntax);
  }
  const size_t start = *pos;
  int depth = 0;
  while (*pos < tokens.size()) {
    if (tokens[*pos] == "(") {
      ++depth;
    } else if (tokens[*pos] == ")") {
      if (depth > 0) {
        --depth;
      }
    } else if (depth == 0 && *pos + 3 < tokens.size() &&
               Upper(tokens[*pos]) == "ON" &&
               Upper(tokens[*pos + 1]) == "DUPLICATE" &&
               Upper(tokens[*pos + 2]) == "KEY" &&
               Upper(tokens[*pos + 3]) == "UPDATE") {
      break;
    }
    ++(*pos);
  }
  std::ostringstream oss;
  for (size_t i = start; i < *pos; ++i) {
    if (i > start) {
      oss << ' ';
    }
    oss << tokens[i];
  }
  out->insert_is_select = true;
  out->insert_select_stmt = std::make_shared<SqlStatement>();
  const Status ps = ParseSqlStatement(oss.str(), out->insert_select_stmt.get());
  if (!ps.ok()) {
    return ps;
  }
  return ParseOnDuplicateKeyUpdate(tokens, pos, out);
}

Status ParseInsertSetClause(const std::vector<std::string>& tokens, size_t* pos,
                            SqlStatement* out) {
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "SET") {
    return Status::Syntax("SET expected", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  out->insert_set_form = true;
  out->insert_columns.clear();
  out->insert_values.clear();
  while (*pos < tokens.size() && Upper(tokens[*pos]) != "ON") {
    if (tokens[*pos] == ",") {
      ++(*pos);
      continue;
    }
    const std::string col = tokens[(*pos)++];
    if (*pos >= tokens.size() || tokens[*pos] != "=") {
      return Status::Syntax("expected = in INSERT SET", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("INSERT SET needs value", ParseErrorKind::kSyntax);
    }
    SqlLiteralValue v;
    Status s = ParseLiteral(tokens[*pos], ColumnType::kVarchar, &v);
    if (!s.ok()) {
      return s;
    }
    out->insert_columns.push_back(col);
    out->insert_values.push_back(v);
    ++(*pos);
  }
  if (!out->insert_values.empty()) {
    out->insert_row_sets.push_back(out->insert_values);
  }
  return ParseOnDuplicateKeyUpdate(tokens, pos, out);
}

Status ParseInsertValues(const std::vector<std::string>& tokens, size_t* pos,
                         SqlStatement* out) {
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "INTO") {
    return Status::Syntax("INSERT INTO expected", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  Status ins_s = ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->table);
  if (!ins_s.ok()) {
    return ins_s;
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "SET") {
    return ParseInsertSetClause(tokens, pos, out);
  }
  if (*pos < tokens.size() && tokens[*pos] == "(") {
    ++(*pos);
    while (*pos < tokens.size() && tokens[*pos] != ")") {
      if (tokens[*pos] != ",") {
        out->insert_columns.push_back(tokens[*pos]);
      }
      ++(*pos);
    }
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      return Status::Syntax("expected ) after column list", ParseErrorKind::kSyntax);
    }
    ++(*pos);
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "SELECT") {
    return ParseInsertSelectSql(tokens, pos, out);
  }
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "VALUES") {
    return Status::Syntax("VALUES or SELECT expected", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if (*pos >= tokens.size() || tokens[*pos] != "(") {
    return Status::Syntax("expected (", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  while (*pos < tokens.size() && tokens[*pos] != ")") {
    if (tokens[*pos] == ",") {
      ++(*pos);
      continue;
    }
    SqlLiteralValue v;
    Status s = ParseInsertValueLiteral(tokens, pos, &v);
    if (!s.ok()) {
      return s;
    }
    out->insert_values.push_back(v);
  }
  if (*pos >= tokens.size() || tokens[*pos] != ")") {
    return Status::Syntax("expected )", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if (!out->insert_values.empty()) {
    out->insert_row_sets.push_back(out->insert_values);
  }
  while (*pos < tokens.size() && tokens[*pos] == ",") {
    ++(*pos);
    if (*pos >= tokens.size() || tokens[*pos] != "(") {
      return Status::Syntax("expected ( for additional VALUES row", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    std::vector<SqlLiteralValue> row;
    while (*pos < tokens.size() && tokens[*pos] != ")") {
      if (tokens[*pos] == ",") {
        ++(*pos);
        continue;
      }
      SqlLiteralValue v;
      Status s = ParseInsertValueLiteral(tokens, pos, &v);
      if (!s.ok()) {
        return s;
      }
      row.push_back(v);
    }
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      return Status::Syntax("expected )", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (!row.empty()) {
      out->insert_row_sets.push_back(std::move(row));
    }
  }
  if (!out->insert_row_sets.empty()) {
    out->insert_values = out->insert_row_sets.front();
  }
  if (out->insert_columns.empty() && out->insert_values.size() >= 2) {
    out->insert_key = SqlLiteralToString(out->insert_values[0], ColumnType::kVarchar);
    out->insert_value =
        SqlLiteralToString(out->insert_values[1], ColumnType::kText);
  }
  Status dup = ParseOnDuplicateKeyUpdate(tokens, pos, out);
  if (!dup.ok()) {
    return dup;
  }
  return Status::OK();
}

Status ParseSimpleWhere(const std::vector<std::string>& tokens, size_t* pos,
                        SqlStatement* out) {
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "WHERE") {
    return Status::OK();
  }
  ++(*pos);
  if (*pos + 2 >= tokens.size()) {
    return Status::Syntax("incomplete WHERE", ParseErrorKind::kSyntax);
  }
  Predicate p;
  p.column = tokens[(*pos)++];
  Status s = ParseCompareOp(tokens[(*pos)++], &p.op);
  if (!s.ok()) {
    return s;
  }
  p.value = Unquote(tokens[(*pos)++]);
  out->where = p;
  ColumnPredicate cp;
  cp.column = p.column;
  cp.op = p.op;
  cp.value = p.value;
  out->where_and.push_back(cp);
  if (BareColumnToken(p.column) == "key") {
    KeyScanRange range;
    range.start.clear();
    range.end_exclusive = "\xFF";
    ApplyKeyBound(&range, p.op, p.value);
    out->key_range = range;
  }
  return Status::OK();
}


}  // namespace detail
}  // namespace heterodb::sql_parse
