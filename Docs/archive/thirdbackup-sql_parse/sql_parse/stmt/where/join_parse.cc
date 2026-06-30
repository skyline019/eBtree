// ParseConcept | JOIN / table alias / FK clause helpers.
#include "sql_parse/stmt/parse_common_api.h"
#include "sql_parse/stmt/select/select_api.h"
#include "sql_parse/stmt/where/join_on_api.h"
#include "sql_parse/stmt/where/where_api.h"

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
#include "common/parse_error.h"
#include "concept/schema/schema.h"
#include "sql_parse/shared/parse_shared.h"
#include "sql_parse/shared/subquery_attach.h"
#include "sql_parse/stmt/parse_statement.h"

namespace heterodb::sql_parse {
namespace detail {
bool IsJoinLeadToken(const std::string& tok) {
  const std::string u = Upper(tok);
  return u == "JOIN" || u == "LEFT" || u == "RIGHT" || u == "FULL" ||
         u == "INNER" || u == "CROSS" || u == "NATURAL";
}

Status ParseFkReferentialAction(const std::vector<std::string>& tokens,
                                size_t* pos, FkOnDelete* out) {
  if (*pos >= tokens.size()) {
    return Status::Syntax("referential action expected", ParseErrorKind::kSyntax);
  }
  const std::string action = Upper(tokens[(*pos)++]);
  if (action == "CASCADE") {
    *out = FkOnDelete::kCascade;
    return Status::OK();
  }
  if (action == "RESTRICT") {
    *out = FkOnDelete::kRestrict;
    return Status::OK();
  }
  if (action == "SET" && *pos < tokens.size() && Upper(tokens[*pos]) == "NULL") {
    ++(*pos);
    *out = FkOnDelete::kSetNull;
    return Status::OK();
  }
  if (action == "NO") {
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "ACTION") {
      ++(*pos);
      *out = FkOnDelete::kNoAction;
      return Status::OK();
    }
    return Status::Syntax("ACTION expected after NO", ParseErrorKind::kSyntax);
  }
  return Status::Syntax("unsupported referential action", ParseErrorKind::kSyntax);
}

Status ParseFkOnDeleteClause(const std::vector<std::string>& tokens, size_t* pos,
                             FkOnDelete* out) {
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "ON") {
    return Status::OK();
  }
  const size_t save = *pos;
  ++(*pos);
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "DELETE") {
    *pos = save;
    return Status::OK();
  }
  ++(*pos);
  return ParseFkReferentialAction(tokens, pos, out);
}

Status ParseFkOnUpdateClause(const std::vector<std::string>& tokens, size_t* pos,
                             FkOnUpdate* out) {
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "ON") {
    return Status::OK();
  }
  const size_t save = *pos;
  ++(*pos);
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "UPDATE") {
    *pos = save;
    return Status::OK();
  }
  ++(*pos);
  return ParseFkReferentialAction(tokens, pos, out);
}

bool IsReservedTableAlias(const std::string& tok) {
  const std::string u = Upper(tok);
  return u == "KEY" || u == "VALUE" || u == "SELECT" || u == "FROM" ||
         u == "WHERE" || u == "AND" || u == "OR" || u == "NOT" || u == "IN" ||
         u == "EXISTS" || u == "BETWEEN" || u == "LIKE" || u == "IS" ||
         u == "NULL" || u == "TRUE" || u == "FALSE" || u == "AS" ||
         u == "ON" || u == "USING" || u == "SET" || u == "INTO" ||
         u == "VALUES" || u == "LIMIT" || u == "OFFSET" || u == "DISTINCT" ||
         u == "OVER" || u == "WINDOW" || u == "ROWS" || u == "RANGE" ||
         u == "PRECEDING" || u == "FOLLOWING" || u == "CURRENT" || u == "ROW" ||
         u == "UNBOUNDED" || u == "PARTITION" || u == "CASE" || u == "WHEN" ||
         u == "THEN" || u == "ELSE" || u == "END" || u == "WITH" ||
         u == "RECURSIVE" || u == "UNION" || u == "ALL" || u == "INNER" ||
         u == "LEFT" || u == "RIGHT" || u == "FULL" || u == "OUTER" ||
         u == "JOIN" || u == "CROSS" || u == "NATURAL" || u == "GROUP" ||
         u == "ORDER" || u == "HAVING" || u == "BY" || u == "ASC" ||
         u == "DESC" || u == "FOR" || u == "UPDATE" || u == "NOWAIT" ||
         u == "SKIP" || u == "LOCKED" || u == "LOCK" || u == "SHARE" ||
         u == "MODE" || u == "PRIMARY" || u == "FOREIGN" || u == "REFERENCES" ||
         u == "CONSTRAINT" || u == "CASCADE" || u == "ACTION" ||
         u == "DEFAULT" || u == "CHECK" || u == "UNIQUE" ||
         u == "INDEX" || u == "TABLE" || u == "CREATE" || u == "DROP" ||
         u == "ALTER" || u == "BEGIN" || u == "COMMIT" || u == "ROLLBACK" ||
         u == "=" || u == ">" || u == "<" || u == ">=" || u == "<=" ||
         u == "<>" || u == "!=";
}

bool IsSetOpLeadToken(const std::string& tok);

bool IsTableAliasStopToken(const std::string& tok) {
  return IsJoinLeadToken(tok) || IsSetOpLeadToken(tok) || IsWhereStopToken(tok) ||
         Upper(tok) == "ON" || IsReservedTableAlias(tok) ||
         (!tok.empty() && tok.front() == '\'');
}

Status ExtractParenthesizedSelectSql(const std::vector<std::string>& tokens,
                                     size_t* pos, std::string* sql_out) {
  if (*pos >= tokens.size() || tokens[*pos] != "(") {
    return Status::Syntax("expected (", ParseErrorKind::kSyntax);
  }
  if (*pos + 1 >= tokens.size() || Upper(tokens[*pos + 1]) != "SELECT") {
    return Status::Syntax("subquery must start with SELECT", ParseErrorKind::kSyntax);
  }
  int depth = 0;
  const size_t start = *pos;
  size_t end = start;
  for (; end < tokens.size(); ++end) {
    if (tokens[end] == "(") {
      ++depth;
    } else if (tokens[end] == ")") {
      --depth;
      if (depth == 0) {
        break;
      }
    }
  }
  if (end >= tokens.size()) {
    return Status::Syntax("missing ) in subquery", ParseErrorKind::kSyntax);
  }
  std::ostringstream oss;
  for (size_t i = start + 1; i < end; ++i) {
    if (i > start + 1) {
      oss << ' ';
    }
    oss << tokens[i];
  }
  *sql_out = oss.str();
  *pos = end + 1;
  return Status::OK();
}

Status ParseRequiredDerivedAlias(const std::vector<std::string>& tokens,
                                size_t* pos, std::string* alias_out) {
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "AS") {
    return Status::Syntax("derived table requires AS alias", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if (*pos >= tokens.size()) {
    return Status::Syntax("missing alias", ParseErrorKind::kSyntax);
  }
  *alias_out = tokens[(*pos)++];
  return Status::OK();
}

Status ParseOptionalTableAlias(
    const std::vector<std::string>& tokens, size_t* pos,
    const std::string& base_table, std::string* outer_name,
    std::unordered_map<std::string, std::string>* aliases) {
  std::string alias;
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "AS") {
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("expected alias after AS", ParseErrorKind::kSyntax);
    }
    alias = tokens[(*pos)++];
  } else if (*pos < tokens.size() && !IsTableAliasStopToken(tokens[*pos])) {
    alias = tokens[(*pos)++];
  }
  if (!alias.empty()) {
    if (aliases != nullptr) {
      (*aliases)[alias] = base_table;
    }
    *outer_name = alias;
  } else {
    *outer_name = base_table;
  }
  return Status::OK();
}

Status ParseJoinUsingColumns(const std::vector<std::string>& tokens, size_t* pos,
                             JoinClause* join) {
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "USING") {
    return Status::Syntax("expected USING", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if (*pos >= tokens.size() || tokens[*pos] != "(") {
    return Status::Syntax("expected ( after USING", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  while (*pos < tokens.size() && tokens[*pos] != ")") {
    if (tokens[*pos] == ",") {
      ++(*pos);
      continue;
    }
    join->using_columns.push_back(tokens[(*pos)++]);
  }
  if (*pos >= tokens.size() || tokens[*pos] != ")") {
    return Status::Syntax("USING missing )", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if (join->using_columns.empty()) {
    return Status::Syntax("USING requires at least one column", ParseErrorKind::kSyntax);
  }
  join->left_column = join->using_columns[0];
  join->right_column = join->using_columns[0];
  return Status::OK();
}

Status ParseJoinOnColumns(const std::vector<std::string>& tokens, size_t* pos,
                          JoinClause* join) {
  return ParseJoinOnCondition(tokens, pos, join);
}

Status ParseJoinPostTable(const std::vector<std::string>& tokens, size_t* pos,
                          JoinClause* join) {
  if (join->join_kind == JoinKind::kCross) {
    return Status::OK();
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "USING") {
    return ParseJoinUsingColumns(tokens, pos, join);
  }
  if (join->natural_join) {
    return Status::OK();
  }
  return ParseJoinOnColumns(tokens, pos, join);
}

Status ParseOneJoinClause(const std::vector<std::string>& tokens, size_t* pos,
                          JoinClause* join, SqlStatement* out) {
  join->join_kind = JoinKind::kInner;
  join->using_columns.clear();
  join->natural_join = false;
  if (*pos >= tokens.size()) {
    return Status::Syntax("JOIN requires table", ParseErrorKind::kSyntax);
  }
  if (Upper(tokens[*pos]) == "NATURAL") {
    ++(*pos);
    join->natural_join = true;
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "CROSS") {
    ++(*pos);
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "JOIN") {
      return Status::Syntax("CROSS JOIN expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    join->join_kind = JoinKind::kCross;
  } else if (Upper(tokens[*pos]) == "LEFT") {
    ++(*pos);
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "OUTER") {
      ++(*pos);
    }
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "JOIN") {
      return Status::Syntax("LEFT JOIN expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    join->join_kind = JoinKind::kLeftOuter;
  } else if (Upper(tokens[*pos]) == "RIGHT") {
    ++(*pos);
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "OUTER") {
      ++(*pos);
    }
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "JOIN") {
      return Status::Syntax("RIGHT JOIN expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    join->join_kind = JoinKind::kRightOuter;
  } else if (Upper(tokens[*pos]) == "FULL") {
    ++(*pos);
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "OUTER") {
      ++(*pos);
    }
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "JOIN") {
      return Status::Syntax("FULL JOIN expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    join->join_kind = JoinKind::kFullOuter;
  } else {
    if (Upper(tokens[*pos]) == "INNER") {
      ++(*pos);
    }
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "JOIN") {
      ++(*pos);
    }
  }
  if (*pos >= tokens.size()) {
    return Status::Syntax("JOIN requires table", ParseErrorKind::kSyntax);
  }
  if (tokens[*pos] == "(") {
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
    join->right_table = alias;
    join->right_alias = alias;
    auto nested = std::make_shared<SqlStatement>();
    const Status ps = ParseSqlStatement(sql, nested.get());
    if (!ps.ok()) {
      return ps;
    }
    AttachSubqueryToJoin(nested, join);
    if (out != nullptr) {
      out->derived_table_queries[alias] = nested;
      out->outer_table_names.push_back(alias);
      if (out->table_aliases.find(alias) == out->table_aliases.end()) {
        out->table_aliases[alias] = alias;
      }
    }
    return ParseJoinPostTable(tokens, pos, join);
  }
  const std::string base_table = tokens[(*pos)++];
  join->right_table = base_table;
  std::string outer_name = base_table;
  Status as = ParseOptionalTableAlias(tokens, pos, base_table, &outer_name,
                                      out != nullptr ? &out->table_aliases
                                                       : nullptr);
  if (!as.ok()) {
    return as;
  }
  join->right_alias = outer_name != base_table ? outer_name : "";
  if (out != nullptr) {
    out->outer_table_names.push_back(outer_name);
  }
  return ParseJoinPostTable(tokens, pos, join);
}
Status ParseRecursiveCte(const std::vector<std::string>& tokens, size_t* pos,
                         SqlStatement* out) {
  if (*pos >= tokens.size()) {
    return Status::Syntax("RECURSIVE CTE requires name", ParseErrorKind::kSyntax);
  }
  const std::string cte_name = tokens[(*pos)++];
  if (*pos + 2 >= tokens.size() || Upper(tokens[*pos]) != "AS" ||
      tokens[*pos + 1] != "(") {
    return Status::Syntax("RECURSIVE name AS ( expected", ParseErrorKind::kSyntax);
  }
  *pos += 2;
  const size_t anchor_start = *pos;
  int depth = 1;
  size_t union_at = std::string::npos;
  while (*pos < tokens.size() && depth > 0) {
    if (tokens[*pos] == "(") {
      ++depth;
    } else if (tokens[*pos] == ")") {
      --depth;
      if (depth == 0) {
        break;
      }
    } else if (depth == 1 && Upper(tokens[*pos]) == "UNION" &&
               *pos + 1 < tokens.size() && Upper(tokens[*pos + 1]) == "ALL") {
      union_at = *pos;
      break;
    }
    ++(*pos);
  }
  if (union_at == std::string::npos) {
    return Status::Syntax("RECURSIVE CTE requires UNION ALL", ParseErrorKind::kSyntax);
  }
  std::ostringstream anchor_sql;
  for (size_t i = anchor_start; i < union_at; ++i) {
    if (i > anchor_start) {
      anchor_sql << ' ';
    }
    anchor_sql << tokens[i];
  }
  *pos = union_at + 2;
  const size_t rec_start = *pos;
  depth = 1;
  while (*pos < tokens.size() && depth > 0) {
    if (tokens[*pos] == "(") {
      ++depth;
    } else if (tokens[*pos] == ")") {
      --depth;
      if (depth == 0) {
        break;
      }
    }
    ++(*pos);
  }
  std::ostringstream rec_sql;
  for (size_t i = rec_start; i < *pos; ++i) {
    if (i > rec_start) {
      rec_sql << ' ';
    }
    rec_sql << tokens[i];
  }
  if (*pos >= tokens.size() || tokens[*pos] != ")") {
    return Status::Syntax("RECURSIVE CTE missing )", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  const std::string rec_body = rec_sql.str();
  if (rec_body.find(cte_name) == std::string::npos) {
    return Status::Syntax("recursive member must reference CTE name", ParseErrorKind::kSyntax);
  }
  out->with_recursive = true;
  out->recursive_cte.name = cte_name;
  const std::string anchor_body = anchor_sql.str();
  auto anchor_stmt = std::make_shared<SqlStatement>();
  Status as = ParseSqlStatement(anchor_body, anchor_stmt.get());
  if (!as.ok()) {
    return as;
  }
  auto recursive_stmt = std::make_shared<SqlStatement>();
  Status rs = ParseSqlStatement(rec_body, recursive_stmt.get());
  if (!rs.ok()) {
    return rs;
  }
  out->recursive_cte.anchor_stmt = std::move(anchor_stmt);
  out->recursive_cte.recursive_stmt = std::move(recursive_stmt);
  out->cte_names.push_back(cte_name);
  return Status::OK();
}

}  // namespace detail
}  // namespace heterodb::sql_parse
