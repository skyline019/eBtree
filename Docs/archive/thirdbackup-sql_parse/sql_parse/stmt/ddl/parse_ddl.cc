#include "sql_parse/stmt/ddl/ddl_api.h"

#include "common/parse_error.h"
#include "sql_parse/stmt/ddl/check_api.h"
#include "sql_parse/stmt/parse_common_api.h"
#include "sql_parse/stmt/where/where_api.h"
#include "sql_parse/stmt/parse_statement.h"

#include "concept/query/parser.h"
#include "concept/query/expr.h"
#include "concept/query/stmt_display_sql.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "concept/catalog/catalog_codec.h"
#include "concept/catalog/qualified_name.h"
#include "concept/catalog/value_codec.h"
#include "concept/schema/schema.h"
#include "sql_parse/adapters/parse_schema_bridge.h"
#include "sql_parse/core/parse_context.h"
#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {
Status ParseColumnTypeTok(const std::vector<std::string>& tokens, size_t* pos,
                        ColumnType* type, uint32_t* max_len) {
  if (*pos >= tokens.size()) {
    return Status::Syntax("type token missing", ParseErrorKind::kSyntax);
  }
  std::string u = Upper(tokens[(*pos)++]);
  if (u == "INT") {
    *type = ColumnType::kInt;
    *max_len = 4;
    return Status::OK();
  }
  if (u == "BIGINT") {
    *type = ColumnType::kBigInt;
    *max_len = 8;
    return Status::OK();
  }
  if (u == "VARCHAR" || u.rfind("VARCHAR", 0) == 0) {
    *type = ColumnType::kVarchar;
    *max_len = 64;
    if (u.find('(') != std::string::npos) {
      *max_len = ColumnTypeFromString(u, max_len) == ColumnType::kVarchar
                     ? *max_len
                     : 64;
      return Status::OK();
    }
    while (*pos < tokens.size() && tokens[*pos] != "," && tokens[*pos] != ")" &&
           tokens[*pos] != "NOT" && Upper(tokens[*pos]) != "PRIMARY" &&
           Upper(tokens[*pos]) != "UNIQUE" && Upper(tokens[*pos]) != "DEFAULT" &&
           Upper(tokens[*pos]) != "UNSIGNED" &&
           Upper(tokens[*pos]) != "AUTO_INCREMENT") {
      if (tokens[*pos] == "(") {
        ++(*pos);
        if (*pos < tokens.size()) {
          *max_len = static_cast<uint32_t>(std::stoul(tokens[(*pos)++]));
        }
        while (*pos < tokens.size() && tokens[*pos] != ")") {
          ++(*pos);
        }
        if (*pos < tokens.size() && tokens[*pos] == ")") {
          ++(*pos);
        }
        break;
      }
      ++(*pos);
    }
    return Status::OK();
  }
  if (u == "TEXT") {
    *type = ColumnType::kText;
    return Status::OK();
  }
  if (u == "DOUBLE") {
    *type = ColumnType::kDouble;
    return Status::OK();
  }
  if (u == "BOOL") {
    *type = ColumnType::kBool;
    return Status::OK();
  }
  if (u == "DATE") {
    *type = ColumnType::kDate;
    return Status::OK();
  }
  if (u == "TIMESTAMP") {
    *type = ColumnType::kTimestamp;
    return Status::OK();
  }
  if (u == "DATETIME") {
    *type = ColumnType::kDatetime;
    return Status::OK();
  }
  if (u == "DECIMAL" || u.rfind("DECIMAL", 0) == 0) {
    *type = ColumnType::kDecimal;
    *max_len = 16;
    if (*pos + 3 < tokens.size() && tokens[*pos] == "(") {
      ++(*pos);
      const uint32_t p = static_cast<uint32_t>(std::stoul(tokens[(*pos)++]));
      if (*pos < tokens.size() && tokens[*pos] == ",") {
        ++(*pos);
        (void)std::stoul(tokens[(*pos)++]);
      }
      if (*pos < tokens.size() && tokens[*pos] == ")") {
        ++(*pos);
      }
      (void)p;
    }
    return Status::OK();
  }
  if (u == "JSON") {
    *type = ColumnType::kJson;
    return Status::OK();
  }
  if (u == "ENUM") {
    *type = ColumnType::kEnum;
    return Status::OK();
  }
  if (u == "BLOB") {
    *type = ColumnType::kBlob;
    return Status::OK();
  }
  if (u == "CHAR" || u.rfind("CHAR", 0) == 0) {
    *type = ColumnType::kVarchar;
    *max_len = 64;
    if (*pos + 2 < tokens.size() && tokens[*pos] == "(") {
      ++(*pos);
      *max_len = static_cast<uint32_t>(std::stoul(tokens[(*pos)++]));
      if (*pos < tokens.size() && tokens[*pos] == ")") {
        ++(*pos);
      }
    }
    return Status::OK();
  }
  return Status::Syntax("unknown type: " + u, ParseErrorKind::kSyntax);
}

Status ParseCreateView(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out) {
  out->kind = SqlStatementKind::kCreateView;
  out->view_column_names.clear();
  if (*pos >= tokens.size()) {
    return Status::Syntax("CREATE VIEW needs name", ParseErrorKind::kSyntax);
  }
  Status view_s = ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->view_name);
  if (!view_s.ok()) {
    return view_s;
  }
  if (*pos < tokens.size() && tokens[*pos] == "(") {
    ++(*pos);
    while (*pos < tokens.size() && tokens[*pos] != ")") {
      if (tokens[*pos] == ",") {
        ++(*pos);
        continue;
      }
      out->view_column_names.push_back(tokens[(*pos)++]);
    }
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      return Status::Syntax("VIEW column list missing )", ParseErrorKind::kSyntax);
    }
    ++(*pos);
  }
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "AS") {
    return Status::Syntax("CREATE VIEW name AS expected", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "SELECT") {
    return Status::Syntax("VIEW body must be SELECT", ParseErrorKind::kSyntax);
  }
  const size_t body_start = *pos;
  std::ostringstream subsql;
  for (size_t i = body_start; i < tokens.size(); ++i) {
    if (i > body_start) {
      subsql << ' ';
    }
    subsql << tokens[i];
  }
  std::ostringstream blob;
  for (size_t i = body_start; i < tokens.size(); ++i) {
    if (i > body_start) {
      blob << '\x1f';
    }
    blob << tokens[i];
  }
  out->view_catalog_token_blob = blob.str();
  *pos = tokens.size();
  auto body = std::make_shared<SqlStatement>();
  Status s = ParseSqlStatement(subsql.str(), body.get());
  if (!s.ok()) {
    return s;
  }
  if (body->kind != SqlStatementKind::kSelect) {
    return Status::Syntax("VIEW body must be SELECT", ParseErrorKind::kSyntax);
  }
  out->view_select_stmt = std::move(body);
  if (FormatStatementDisplaySql(*out->view_select_stmt).empty()) {
    return Status::Syntax("VIEW body cannot be formatted for display", ParseErrorKind::kSyntax);
  }
  return Status::OK();
}

Status ParseCreateTable(const std::vector<std::string>& tokens, size_t* pos,
                        SqlStatement* out) {
  out->kind = SqlStatementKind::kCreateTable;
  out->create_if_not_exists = false;
  if (*pos >= tokens.size()) {
    return Status::Syntax("CREATE TABLE needs name", ParseErrorKind::kSyntax);
  }
  if (Upper(tokens[*pos]) == "IF") {
    if (*pos + 3 >= tokens.size() || Upper(tokens[*pos + 1]) != "NOT" ||
        Upper(tokens[*pos + 2]) != "EXISTS") {
      return Status::Syntax("IF NOT EXISTS expected", ParseErrorKind::kSyntax);
    }
    *pos += 3;
    out->create_if_not_exists = true;
    if (*pos >= tokens.size()) {
      return Status::Syntax("CREATE TABLE needs name", ParseErrorKind::kSyntax);
    }
  }
  Status qn_s = ResolveQualifiedTableToken(tokens[(*pos)++], &out->create_table.database,
                                           &out->create_table.name);
  if (!qn_s.ok()) {
    return qn_s;
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "LIKE") {
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("LIKE needs source table", ParseErrorKind::kSyntax);
    }
    out->create_table_like = true;
    return ResolveQualifiedTableToken(tokens[(*pos)++], &out->create_table.database,
                                    &out->create_table_like_name);
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "AS") {
    ++(*pos);
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "SELECT") {
      return Status::Syntax("AS SELECT expected", ParseErrorKind::kSyntax);
    }
    std::ostringstream sql;
    for (size_t i = *pos; i < tokens.size(); ++i) {
      if (i > *pos) {
        sql << ' ';
      }
      sql << tokens[i];
    }
    out->create_table_as_select = true;
    auto body = std::make_shared<SqlStatement>();
    Status ps = ParseSqlStatement(sql.str(), body.get());
    if (!ps.ok()) {
      return ps;
    }
    if (body->kind != SqlStatementKind::kSelect) {
      return Status::Syntax("CTAS body must be SELECT", ParseErrorKind::kSyntax);
    }
    out->create_table_as_select_stmt = std::move(body);
    if (FormatStatementDisplaySql(*out->create_table_as_select_stmt).empty()) {
      return Status::Syntax("CTAS body cannot be formatted for display", ParseErrorKind::kSyntax);
    }
    *pos = tokens.size();
    return Status::OK();
  }
  if (*pos >= tokens.size() || tokens[*pos] != "(") {
    return Status::Syntax("expected (", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  TableDef t;
  t.database = out->create_table.database;
  t.name = out->create_table.name;
  uint32_t ord = 0;
  while (*pos < tokens.size() && tokens[*pos] != ")") {
    if (tokens[*pos] == ",") {
      ++(*pos);
      continue;
    }
    if (Upper(tokens[*pos]) == "FOREIGN") {
      ++(*pos);
      if (*pos >= tokens.size() || Upper(tokens[*pos]) != "KEY") {
        return Status::Syntax("FOREIGN KEY expected", ParseErrorKind::kSyntax);
      }
      ++(*pos);
      ForeignKeyDef fk;
      fk.name = "fk_" + std::to_string(t.foreign_keys.size());
      if (*pos >= tokens.size() || tokens[*pos] != "(") {
        return Status::Syntax("FOREIGN KEY ( cols ) expected", ParseErrorKind::kSyntax);
      }
      ++(*pos);
      while (*pos < tokens.size() && tokens[*pos] != ")") {
        if (tokens[*pos] != ",") {
          fk.columns.push_back(tokens[(*pos)++]);
        } else {
          ++(*pos);
        }
      }
      ++(*pos);
      if (*pos >= tokens.size() || Upper(tokens[*pos]) != "REFERENCES") {
        return Status::Syntax("REFERENCES expected", ParseErrorKind::kSyntax);
      }
      ++(*pos);
      Status ref_s = ResolveQualifiedTableToken(tokens[(*pos)++], &fk.ref_database,
                                                &fk.ref_table);
      if (!ref_s.ok()) {
        return ref_s;
      }
      if (fk.ref_database.empty()) {
        fk.ref_database = t.database;
      }
      if (*pos < tokens.size() && tokens[*pos] == "(") {
        ++(*pos);
        while (*pos < tokens.size() && tokens[*pos] != ")") {
          if (tokens[*pos] != ",") {
            fk.ref_columns.push_back(tokens[(*pos)++]);
          } else {
            ++(*pos);
          }
        }
        ++(*pos);
      }
      Status ods = ParseFkOnDeleteClause(tokens, pos, &fk.on_delete);
      if (!ods.ok()) {
        return ods;
      }
      Status ous = ParseFkOnUpdateClause(tokens, pos, &fk.on_update);
      if (!ous.ok()) {
        return ous;
      }
      t.foreign_keys.push_back(std::move(fk));
      continue;
    }
    if (Upper(tokens[*pos]) == "CONSTRAINT") {
      ++(*pos);
      if (*pos >= tokens.size()) {
        return Status::Syntax("CONSTRAINT name expected", ParseErrorKind::kSyntax);
      }
      const std::string cname = tokens[(*pos)++];
      if (*pos >= tokens.size()) {
        return Status::Syntax("CONSTRAINT body expected", ParseErrorKind::kSyntax);
      }
      if (Upper(tokens[*pos]) == "FOREIGN") {
        ++(*pos);
        if (*pos >= tokens.size() || Upper(tokens[*pos]) != "KEY") {
          return Status::Syntax("FOREIGN KEY expected", ParseErrorKind::kSyntax);
        }
        ++(*pos);
        ForeignKeyDef fk;
        fk.name = cname;
        if (*pos >= tokens.size() || tokens[*pos] != "(") {
          return Status::Syntax("FOREIGN KEY ( cols ) expected", ParseErrorKind::kSyntax);
        }
        ++(*pos);
        while (*pos < tokens.size() && tokens[*pos] != ")") {
          if (tokens[*pos] != ",") {
            fk.columns.push_back(tokens[(*pos)++]);
          } else {
            ++(*pos);
          }
        }
        ++(*pos);
        if (*pos >= tokens.size() || Upper(tokens[*pos]) != "REFERENCES") {
          return Status::Syntax("REFERENCES expected", ParseErrorKind::kSyntax);
        }
        ++(*pos);
        Status ref_s = ResolveQualifiedTableToken(tokens[(*pos)++], &fk.ref_database,
                                                &fk.ref_table);
        if (!ref_s.ok()) {
          return ref_s;
        }
        if (fk.ref_database.empty()) {
          fk.ref_database = t.database;
        }
        if (*pos < tokens.size() && tokens[*pos] == "(") {
          ++(*pos);
          while (*pos < tokens.size() && tokens[*pos] != ")") {
            if (tokens[*pos] != ",") {
              fk.ref_columns.push_back(tokens[(*pos)++]);
            } else {
              ++(*pos);
            }
          }
          ++(*pos);
        }
        Status ods = ParseFkOnDeleteClause(tokens, pos, &fk.on_delete);
        if (!ods.ok()) {
          return ods;
        }
        Status ous = ParseFkOnUpdateClause(tokens, pos, &fk.on_update);
        if (!ous.ok()) {
          return ous;
        }
        t.foreign_keys.push_back(std::move(fk));
        continue;
      }
      if (Upper(tokens[*pos]) == "CHECK") {
        ++(*pos);
        CheckConstraintDef chk;
        Status cs = ParseCheckConstraintBody(tokens, pos, cname, &chk);
        if (!cs.ok()) {
          return cs;
        }
        t.check_constraints.push_back(std::move(chk));
        continue;
      }
      return Status::Syntax("unsupported CONSTRAINT type", ParseErrorKind::kSyntax);
    }
    ColumnDef col;
    col.name = tokens[(*pos)++];
    if (*pos >= tokens.size()) {
      return Status::Syntax("column type expected", ParseErrorKind::kSyntax);
    }
    uint32_t max_len = 64;
    Status s = ParseColumnTypeTok(tokens, pos, &col.type, &max_len);
    if (!s.ok()) {
      return s;
    }
    col.max_length = max_len;
    if (col.type == ColumnType::kEnum && *pos < tokens.size() && tokens[*pos] == "(") {
      ++(*pos);
      while (*pos < tokens.size() && tokens[*pos] != ")") {
        if (tokens[*pos] != ",") {
          col.enum_values.push_back(Unquote(tokens[(*pos)++]));
        } else {
          ++(*pos);
        }
      }
      if (*pos >= tokens.size() || tokens[*pos] != ")") {
        return Status::Syntax("ENUM missing )", ParseErrorKind::kSyntax);
      }
      ++(*pos);
    }
    while (*pos < tokens.size() && tokens[*pos] != "," && tokens[*pos] != ")") {
      const std::string mod = Upper(tokens[*pos]);
      if (mod == "PRIMARY" && *pos + 1 < tokens.size() &&
          Upper(tokens[*pos + 1]) == "KEY") {
        col.primary_key = true;
        col.not_null = true;
        col.nullable = false;
        *pos += 2;
      } else if (mod == "NOT" && *pos + 1 < tokens.size() &&
                 Upper(tokens[*pos + 1]) == "NULL") {
        col.not_null = true;
        col.nullable = false;
        *pos += 2;
      } else if (mod == "UNIQUE") {
        col.unique = true;
        ++(*pos);
      } else if (mod == "DEFAULT" && *pos + 1 < tokens.size()) {
        col.default_literal = tokens[++(*pos)];
        ++(*pos);
      } else if (mod == "UNSIGNED") {
        col.unsigned_col = true;
        ++(*pos);
      } else if (mod == "AUTO_INCREMENT") {
        col.auto_increment = true;
        ++(*pos);
      } else {
        break;
      }
    }
    col.ordinal = ord++;
    t.columns.push_back(col);
  }
  if (*pos >= tokens.size() || tokens[*pos] != ")") {
    return Status::Syntax("expected )", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  while (*pos < tokens.size() && Upper(tokens[*pos]) == "STORAGE") {
    ++(*pos);
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "ENGINE") {
      return Status::Syntax("STORAGE ENGINE expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size() || tokens[*pos] != "=") {
      return Status::Syntax("STORAGE ENGINE = name expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("STORAGE ENGINE name expected", ParseErrorKind::kSyntax);
    }
    bool ok = false;
    t.storage_engine = ParseStorageEngineKindName(tokens[(*pos)++], &ok);
    if (!ok) {
      return Status::Syntax("unknown STORAGE ENGINE (use LSM or BTREE)", ParseErrorKind::kSyntax);
    }
  }
  out->create_table = t;
  out->table_storage_engine = t.storage_engine;
  return Status::OK();
}


}  // namespace detail
}  // namespace heterodb::sql_parse
