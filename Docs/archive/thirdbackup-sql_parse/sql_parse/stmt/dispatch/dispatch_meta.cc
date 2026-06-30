#include "sql_parse/stmt/dispatch/dispatch_api.h"

#include "common/parse_error.h"
#include "sql_parse/stmt/parse_common_api.h"
#include "sql_parse/stmt/parse_statement.h"
#include "sql_parse/adapters/parse_catalog_bridge.h"
#include "sql_parse/shared/parse_shared.h"

#include "concept/query/stmt_display_sql.h"

#include <sstream>

namespace heterodb::sql_parse {
namespace detail {
Status ParseStmtMeta(const std::string& head, const std::vector<std::string>& tokens, size_t* pos, SqlStatement* out)  {
  if (head == "USE") {
    if ((*pos) >= tokens.size()) {
      return Status::Syntax("USE needs database name", ParseErrorKind::kSyntax);
    }
    out->kind = SqlStatementKind::kUseDatabase;
    out->database_name = tokens[(*pos)++];
    return Status::OK();
  }

  if (head == "SHOW") {
    if ((*pos) >= tokens.size()) {
      return Status::Syntax("SHOW needs object", ParseErrorKind::kSyntax);
    }
    if (Upper(tokens[*pos]) == "DATABASES") {
      out->kind = SqlStatementKind::kShowDatabases;
      return Status::OK();
    }
    if (Upper(tokens[*pos]) == "TABLES") {
      out->kind = SqlStatementKind::kShowTables;
      ++(*pos);
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "FROM") {
        ++(*pos);
        if ((*pos) >= tokens.size()) {
          return Status::Syntax("SHOW TABLES FROM needs database", ParseErrorKind::kSyntax);
        }
        out->show_tables_database = tokens[(*pos)++];
      }
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "LIKE") {
        ++(*pos);
        if ((*pos) >= tokens.size()) {
          return Status::Syntax("SHOW TABLES LIKE needs pattern", ParseErrorKind::kSyntax);
        }
        out->show_tables_like_pattern = tokens[(*pos)++];
      }
      return Status::OK();
    }
    if (Upper(tokens[*pos]) == "VIEWS") {
      out->kind = SqlStatementKind::kShowViews;
      ++(*pos);
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "FROM") {
        ++(*pos);
        if ((*pos) >= tokens.size()) {
          return Status::Syntax("SHOW VIEWS FROM needs database", ParseErrorKind::kSyntax);
        }
        out->show_views_database = tokens[(*pos)++];
      }
      return Status::OK();
    }
    if (Upper(tokens[*pos]) == "CREATE") {
      ++(*pos);
      if ((*pos) >= tokens.size()) {
        return Status::Syntax("SHOW CREATE needs VIEW or TABLE", ParseErrorKind::kSyntax);
      }
      const std::string object = Upper(tokens[*pos]);
      if (object == "VIEW") {
        ++(*pos);
        if ((*pos) >= tokens.size()) {
          return Status::Syntax("SHOW CREATE VIEW needs name", ParseErrorKind::kSyntax);
        }
        out->kind = SqlStatementKind::kShowCreateView;
        return ResolveQualifiedTableToken(tokens[(*pos)++], &out->database,
                                          &out->view_name);
      }
      if (object == "TABLE") {
        ++(*pos);
        if ((*pos) >= tokens.size()) {
          return Status::Syntax("SHOW CREATE TABLE needs name", ParseErrorKind::kSyntax);
        }
        out->kind = SqlStatementKind::kShowCreateTable;
        return ResolveQualifiedTableToken(tokens[(*pos)++], &out->database,
                                          &out->table);
      }
      return Status::Syntax("SHOW CREATE needs VIEW or TABLE", ParseErrorKind::kSyntax);
    }
    if (Upper(tokens[*pos]) == "COLUMNS") {
      out->kind = SqlStatementKind::kShowColumns;
      ++(*pos);
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "EXTENDED") {
        out->show_columns_extended = true;
        ++(*pos);
      }
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "FROM") {
        ++(*pos);
        return ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->table);
      }
      return Status::Syntax("SHOW COLUMNS FROM table expected", ParseErrorKind::kSyntax);
    }
    if (Upper(tokens[*pos]) == "INDEX") {
      out->kind = SqlStatementKind::kShowIndex;
      ++(*pos);
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "FROM") {
        ++(*pos);
        return ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->table);
      }
      return Status::Syntax("SHOW INDEX FROM table expected", ParseErrorKind::kSyntax);
    }
    if (Upper(tokens[*pos]) == "WARNINGS") {
      out->kind = SqlStatementKind::kShowWarnings;
      return Status::OK();
    }
    return Status::Syntax("unsupported SHOW", ParseErrorKind::kSyntax);
  }

  if (head == "DESCRIBE" || head == "DESC") {
    out->kind = SqlStatementKind::kDescribe;
    if ((*pos) >= tokens.size()) {
      return Status::Syntax("DESCRIBE needs table", ParseErrorKind::kSyntax);
    }
    return ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->table);
  }

  if (head == "EXPLAIN") {
    out->kind = SqlStatementKind::kExplainStmt;
    if ((*pos) >= tokens.size()) {
      return Status::Syntax("EXPLAIN needs statement", ParseErrorKind::kSyntax);
    }
    std::ostringstream sql;
    for (size_t i = *pos; i < tokens.size(); ++i) {
      if (i > *pos) {
        sql << ' ';
      }
      sql << tokens[i];
    }
    const std::string inner_sql = sql.str();
    auto inner = std::make_shared<SqlStatement>();
    Status inner_s = ParseSqlStatement(inner_sql, inner.get());
    if (!inner_s.ok()) {
      return inner_s;
    }
    out->explain_inner_stmt = std::move(inner);
    out->explain_inner_kind = out->explain_inner_stmt->kind;
    if (out->explain_inner_stmt->kind == SqlStatementKind::kSelect &&
        FormatStatementDisplaySql(*out->explain_inner_stmt).empty()) {
      return Status::Syntax("EXPLAIN body cannot be formatted for display",
                            ParseErrorKind::kSyntax);
    }
    *pos = tokens.size();
    return Status::OK();
  }
  return Status::Syntax("not handled by ParseStmtMeta", ParseErrorKind::kSyntax);
}

}  // namespace detail
}  // namespace heterodb::sql_parse
