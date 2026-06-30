// ParseConcept | DDL statement routing (CREATE/DROP/ALTER/REINDEX).
#include "common/parse_error.h"
#include "sql_parse/stmt/ddl/alter_api.h"
#include "sql_parse/stmt/ddl/ddl_api.h"
#include "sql_parse/stmt/parse_common_api.h"
#include "sql_parse/stmt/where/where_api.h"
#include "sql_parse/adapters/parse_catalog_bridge.h"
#include "sql_parse/adapters/parse_schema_bridge.h"
#include "sql_parse/shared/parse_shared.h"

#include <sstream>

namespace heterodb::sql_parse {
namespace detail {
Status ParseStmtDdl(const std::string& head, const std::vector<std::string>& tokens, size_t* pos, SqlStatement* out)  {
  if (head == "CREATE") {
    if ((*pos) >= tokens.size()) {
      return Status::Syntax("CREATE needs object", ParseErrorKind::kSyntax);
    }
    bool or_replace = false;
    if (Upper(tokens[*pos]) == "OR") {
      if ((*pos) + 1 >= tokens.size() || Upper(tokens[(*pos) + 1]) != "REPLACE") {
        return Status::Syntax("REPLACE expected after OR", ParseErrorKind::kSyntax);
      }
      (*pos) += 2;
      or_replace = true;
    }
    if (Upper(tokens[*pos]) == "DATABASE") {
      out->kind = SqlStatementKind::kCreateDatabase;
      out->create_db_if_not_exists = false;
      ++(*pos);
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "IF") {
        if ((*pos) + 3 >= tokens.size() || Upper(tokens[(*pos) + 1]) != "NOT" ||
            Upper(tokens[(*pos) + 2]) != "EXISTS") {
          return Status::Syntax("IF NOT EXISTS expected", ParseErrorKind::kSyntax);
        }
        (*pos) += 3;
        out->create_db_if_not_exists = true;
      }
      if ((*pos) >= tokens.size()) {
        return Status::Syntax("CREATE DATABASE needs name", ParseErrorKind::kSyntax);
      }
      out->database_name = tokens[(*pos)++];
      return Status::OK();
    }
    if (Upper(tokens[*pos]) == "TABLE") {
      ++(*pos);
      return ParseCreateTable(tokens, pos, out);
    }
    if (Upper(tokens[*pos]) == "VIEW") {
      ++(*pos);
      out->view_or_replace = or_replace;
      return ParseCreateView(tokens, pos, out);
    }
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "UNIQUE") {
      out->index_unique = true;
      ++(*pos);
    }
    if (Upper(tokens[*pos]) == "INDEX") {
      out->kind = SqlStatementKind::kCreateIndex;
      ++(*pos);
      out->index_name = tokens[(*pos)++];
      if ((*pos) >= tokens.size() || Upper(tokens[*pos]) != "ON") {
        return Status::Syntax("ON expected", ParseErrorKind::kSyntax);
      }
      ++(*pos);
      Status idx_s = ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->table);
      if (!idx_s.ok()) {
        return idx_s;
      }
      if ((*pos) < tokens.size() && tokens[*pos] == "(") {
        ++(*pos);
        while ((*pos) < tokens.size() && tokens[*pos] != ")") {
          if (tokens[*pos] != ",") {
            out->index_columns.push_back(tokens[*pos]);
          }
          ++(*pos);
        }
        ++(*pos);
      }
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "USING") {
        ++(*pos);
        if ((*pos) >= tokens.size()) {
          return Status::Syntax("USING index type expected", ParseErrorKind::kSyntax);
        }
        out->index_using = Upper(tokens[(*pos)++]);
      }
      return Status::OK();
    }
    return Status::Syntax("unsupported CREATE",
                          ParseErrorKind::kUnsupportedStatement);
  }

  if (head == "DROP") {
    if ((*pos) >= tokens.size()) {
      return Status::Syntax("DROP needs object", ParseErrorKind::kSyntax);
    }
    if (Upper(tokens[*pos]) == "DATABASE") {
      out->kind = SqlStatementKind::kDropDatabase;
      ++(*pos);
      if ((*pos) + 1 < tokens.size() && Upper(tokens[*pos]) == "IF" &&
          Upper(tokens[(*pos) + 1]) == "EXISTS") {
        (*pos) += 2;
        out->drop_if_exists = true;
      }
      if ((*pos) >= tokens.size()) {
        return Status::Syntax("DROP DATABASE needs name", ParseErrorKind::kSyntax);
      }
      out->database_name = tokens[(*pos)++];
      return Status::OK();
    }
    if (Upper(tokens[*pos]) == "TABLE") {
      out->kind = SqlStatementKind::kDropTable;
      ++(*pos);
      if ((*pos) + 1 < tokens.size() && Upper(tokens[*pos]) == "IF" &&
          Upper(tokens[(*pos) + 1]) == "EXISTS") {
        (*pos) += 2;
        out->drop_if_exists = true;
      }
      Status drop_s = ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->table);
      return drop_s;
    }
    if (Upper(tokens[*pos]) == "VIEW") {
      out->kind = SqlStatementKind::kDropView;
      ++(*pos);
      if ((*pos) + 1 < tokens.size() && Upper(tokens[*pos]) == "IF" &&
          Upper(tokens[(*pos) + 1]) == "EXISTS") {
        (*pos) += 2;
        out->drop_if_exists = true;
      }
      return ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->view_name);
    }
    if (Upper(tokens[*pos]) == "INDEX") {
      out->kind = SqlStatementKind::kDropIndex;
      ++(*pos);
      out->index_name = tokens[(*pos)++];
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "ON") {
        ++(*pos);
        Status drop_idx_s =
            ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->table);
        if (!drop_idx_s.ok()) {
          return drop_idx_s;
        }
      }
      return Status::OK();
    }
    return Status::Syntax("unsupported DROP",
                          ParseErrorKind::kUnsupportedStatement);
  }

  if (head == "ALTER") {
    return ParseAlterTable(tokens, pos, out);
  }

  if (head == "REINDEX") {
    if ((*pos) >= tokens.size() || Upper(tokens[*pos]) != "TABLE") {
      return Status::Syntax("REINDEX TABLE expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    out->kind = SqlStatementKind::kReindexTable;
    out->table = tokens[(*pos)++];
    return Status::OK();
  }
  return Status::Syntax("not handled by ParseStmtDdl", ParseErrorKind::kSyntax);
}

}  // namespace detail
}  // namespace heterodb::sql_parse
