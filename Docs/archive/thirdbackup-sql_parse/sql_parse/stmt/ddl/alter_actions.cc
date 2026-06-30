// ParseConcept | ALTER TABLE multi-action parsing.
#include "sql_parse/stmt/ddl/alter_api.h"

#include "common/parse_error.h"

#include "sql_parse/adapters/parse_schema_bridge.h"
#include "sql_parse/stmt/ddl/check_api.h"
#include "sql_parse/stmt/ddl/ddl_api.h"
#include "sql_parse/stmt/parse_common_api.h"
#include "sql_parse/stmt/where/where_api.h"
#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {
namespace {

void InitAlterPart(const SqlStatement& header, SqlStatement* part) {
  part->kind = SqlStatementKind::kAlterTable;
  part->database = header.database;
  part->table = header.table;
}

Status ParseSingleAlterAction(const std::vector<std::string>& tokens, size_t* pos,
                              SqlStatement* out) {
  if (*pos >= tokens.size()) {
    return Status::Syntax("ALTER action expected", ParseErrorKind::kSyntax);
  }
  const std::string action = Upper(tokens[(*pos)++]);
  if (action == "RENAME") {
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "COLUMN") {
      ++(*pos);
      if (*pos + 2 >= tokens.size() || Upper(tokens[*pos + 1]) != "TO") {
        return Status::Syntax("RENAME COLUMN old TO new expected", ParseErrorKind::kSyntax);
      }
      out->alter_kind = AlterKind::kRenameColumn;
      out->rename_column_from = tokens[(*pos)++];
      if (*pos >= tokens.size() || Upper(tokens[*pos]) != "TO") {
        return Status::Syntax("RENAME COLUMN old TO new expected", ParseErrorKind::kSyntax);
      }
      ++(*pos);
      if (*pos >= tokens.size()) {
        return Status::Syntax("RENAME COLUMN old TO new expected", ParseErrorKind::kSyntax);
      }
      out->rename_column_to = tokens[(*pos)++];
      return Status::OK();
    }
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "TO") {
      return Status::Syntax("RENAME TO expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    out->alter_kind = AlterKind::kRenameTable;
    out->rename_table_to = tokens[(*pos)++];
    return Status::OK();
  }
  if (action == "ADD") {
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "CONSTRAINT") {
      ++(*pos);
      out->alter_kind = AlterKind::kAddConstraint;
      out->constraint_name = tokens[(*pos)++];
      if (Upper(tokens[*pos]) == "FOREIGN") {
        ++(*pos);
        if (Upper(tokens[*pos]) != "KEY") {
          return Status::Syntax("FOREIGN KEY expected", ParseErrorKind::kSyntax);
        }
        ++(*pos);
        ForeignKeyDef& fk = out->add_foreign_key;
        fk.name = out->constraint_name;
        if (tokens[*pos] != "(") {
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
        if (Upper(tokens[*pos]) != "REFERENCES") {
          return Status::Syntax("REFERENCES expected", ParseErrorKind::kSyntax);
        }
        ++(*pos);
        Status ref_s =
            ResolveQualifiedTableToken(tokens[(*pos)++], &fk.ref_database, &fk.ref_table);
        if (!ref_s.ok()) {
          return ref_s;
        }
        if (fk.ref_database.empty()) {
          fk.ref_database = out->database;
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
        return Status::OK();
      }
      if (Upper(tokens[*pos]) == "CHECK") {
        ++(*pos);
        CheckConstraintDef& chk = out->add_check;
        return ParseCheckConstraintBody(tokens, pos, out->constraint_name, &chk);
      }
      return Status::Syntax("unsupported ADD CONSTRAINT", ParseErrorKind::kSyntax);
    }
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "PRIMARY" &&
        *pos + 1 < tokens.size() && Upper(tokens[*pos + 1]) == "KEY") {
      *pos += 2;
      if (*pos >= tokens.size() || tokens[*pos] != "(") {
        return Status::Syntax("ADD PRIMARY KEY ( cols ) expected", ParseErrorKind::kSyntax);
      }
      ++(*pos);
      out->alter_kind = AlterKind::kAddPrimaryKey;
      while (*pos < tokens.size() && tokens[*pos] != ")") {
        if (tokens[*pos] != ",") {
          out->index_columns.push_back(tokens[(*pos)++]);
        } else {
          ++(*pos);
        }
      }
      if (*pos >= tokens.size() || tokens[*pos] != ")") {
        return Status::Syntax("ADD PRIMARY KEY missing )", ParseErrorKind::kSyntax);
      }
      ++(*pos);
      return Status::OK();
    }
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "COLUMN") {
      return Status::Syntax("ADD COLUMN expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("ADD COLUMN needs name", ParseErrorKind::kSyntax);
    }
    out->alter_kind = AlterKind::kAddColumn;
    out->alter_column.name = tokens[(*pos)++];
    uint32_t ml = 64;
    Status s = ParseColumnTypeTok(tokens, pos, &out->alter_column.type, &ml);
    if (!s.ok()) {
      return s;
    }
    out->alter_column.max_length = ml;
    while (*pos < tokens.size()) {
      const std::string mod = Upper(tokens[*pos]);
      if (mod == "NOT" && *pos + 1 < tokens.size() &&
          Upper(tokens[*pos + 1]) == "NULL") {
        out->alter_column.not_null = true;
        out->alter_column.nullable = false;
        (*pos) += 2;
      } else if (mod == "DEFAULT" && *pos + 1 < tokens.size()) {
        out->alter_column.default_literal = tokens[++(*pos)];
        ++(*pos);
      } else {
        break;
      }
    }
    return Status::OK();
  }
  if (action == "DROP") {
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "CONSTRAINT") {
      ++(*pos);
      out->alter_kind = AlterKind::kDropConstraint;
      if (*pos >= tokens.size()) {
        return Status::Syntax("DROP CONSTRAINT needs name", ParseErrorKind::kSyntax);
      }
      out->constraint_name = tokens[(*pos)++];
      return Status::OK();
    }
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "COLUMN") {
      return Status::Syntax("DROP COLUMN expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("DROP COLUMN needs name", ParseErrorKind::kSyntax);
    }
    out->alter_kind = AlterKind::kDropColumn;
    out->drop_column_name = tokens[(*pos)++];
    return Status::OK();
  }
  if (action == "MODIFY") {
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "COLUMN") {
      return Status::Syntax("MODIFY COLUMN expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("MODIFY COLUMN needs name", ParseErrorKind::kSyntax);
    }
    out->alter_kind = AlterKind::kModifyColumn;
    out->alter_column.name = tokens[(*pos)++];
    uint32_t ml = 64;
    Status s = ParseColumnTypeTok(tokens, pos, &out->alter_column.type, &ml);
    if (!s.ok()) {
      return s;
    }
    out->alter_column.max_length = ml;
    return Status::OK();
  }
  if (action == "CHANGE") {
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "COLUMN") {
      return Status::Syntax("CHANGE COLUMN expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("CHANGE COLUMN needs old name", ParseErrorKind::kSyntax);
    }
    out->alter_kind = AlterKind::kChangeColumn;
    out->rename_column_from = tokens[(*pos)++];
    if (*pos >= tokens.size()) {
      return Status::Syntax("CHANGE COLUMN needs new name", ParseErrorKind::kSyntax);
    }
    out->rename_column_to = tokens[(*pos)++];
    uint32_t ml = 64;
    Status s = ParseColumnTypeTok(tokens, pos, &out->alter_column.type, &ml);
    if (!s.ok()) {
      return s;
    }
    out->alter_column.max_length = ml;
    out->alter_column.name = out->rename_column_to;
    return Status::OK();
  }
  if (action == "SET") {
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "STORAGE") {
      return Status::Syntax("SET STORAGE expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (Upper(tokens[*pos]) != "ENGINE") {
      return Status::Syntax("SET STORAGE ENGINE expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size() || tokens[*pos] != "=") {
      return Status::Syntax("SET STORAGE ENGINE = name expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("SET STORAGE ENGINE name expected", ParseErrorKind::kSyntax);
    }
    bool ok = false;
    out->alter_storage_engine =
        heterodb::sql_parse::ParseStorageEngineKindName(tokens[(*pos)++], &ok);
    if (!ok) {
      return Status::Syntax("unknown STORAGE ENGINE (use LSM or BTREE)", ParseErrorKind::kSyntax);
    }
    out->alter_kind = AlterKind::kSetStorageEngine;
    return Status::OK();
  }
  return Status::Syntax("unsupported ALTER", ParseErrorKind::kSyntax);
}

}  // namespace

Status ParseAlterTable(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out) {
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "TABLE") {
    return Status::Syntax("ALTER TABLE expected", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  out->kind = SqlStatementKind::kAlterTable;
  if (*pos >= tokens.size()) {
    return Status::Syntax("ALTER TABLE needs name", ParseErrorKind::kSyntax);
  }
  Status alt_s = ResolveQualifiedTableToken(tokens[(*pos)++], &out->database, &out->table);
  if (!alt_s.ok()) {
    return alt_s;
  }

  std::vector<SqlStatement> actions;
  while (*pos < tokens.size()) {
    SqlStatement part;
    InitAlterPart(*out, &part);
    Status s = ParseSingleAlterAction(tokens, pos, &part);
    if (!s.ok()) {
      return s;
    }
    actions.push_back(std::move(part));
    if (*pos >= tokens.size() || tokens[*pos] != ",") {
      break;
    }
    ++(*pos);
  }

  if (actions.empty()) {
    return Status::Syntax("ALTER action expected", ParseErrorKind::kSyntax);
  }
  if (actions.size() == 1) {
    *out = std::move(actions.front());
  } else {
    *out = actions.front();
    out->alter_batch = std::move(actions);
  }
  return Status::OK();
}

}  // namespace detail
}  // namespace heterodb::sql_parse
