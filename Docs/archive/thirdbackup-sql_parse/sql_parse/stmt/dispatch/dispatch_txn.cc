#include "sql_parse/stmt/dispatch/dispatch_api.h"

#include "common/parse_error.h"
#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {

Status ParseStmtSetTxn(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out) {
  if (pos == nullptr || out == nullptr) {
    return Status::InvalidArgument("null argument");
  }
  if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "TRANSACTION") {
      ++(*pos);
      out->kind = SqlStatementKind::kSetTransaction;
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "ISOLATION") {
        ++(*pos);
        if ((*pos) >= tokens.size() || Upper(tokens[*pos]) != "LEVEL") {
          return Status::Syntax("ISOLATION LEVEL expected", ParseErrorKind::kSyntax);
        }
        ++(*pos);
        if ((*pos) >= tokens.size()) {
          return Status::Syntax("isolation level expected", ParseErrorKind::kSyntax);
        }
        const std::string lvl = Upper(tokens[(*pos)++]);
        if (lvl == "READ" && (*pos) < tokens.size() &&
            Upper(tokens[*pos]) == "UNCOMMITTED") {
          ++(*pos);
          out->txn_isolation = IsolationLevel::kReadUncommitted;
          return Status::OK();
        }
        if (lvl == "READ" && (*pos) < tokens.size() &&
            Upper(tokens[*pos]) == "COMMITTED") {
          ++(*pos);
          out->txn_isolation = IsolationLevel::kReadCommitted;
          return Status::OK();
        }
        if (lvl == "REPEATABLE" && (*pos) < tokens.size() &&
            Upper(tokens[*pos]) == "READ") {
          ++(*pos);
          out->txn_isolation = IsolationLevel::kRepeatableRead;
          return Status::OK();
        }
        if (lvl == "SERIALIZABLE") {
          out->txn_isolation = IsolationLevel::kSerializable;
          return Status::OK();
        }
        return Status::Syntax("unsupported isolation level", ParseErrorKind::kSyntax);
      }
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "READ") {
        ++(*pos);
        if ((*pos) >= tokens.size() || Upper(tokens[*pos]) != "ONLY") {
          return Status::Syntax("READ ONLY expected", ParseErrorKind::kSyntax);
        }
        ++(*pos);
        out->txn_read_only = true;
        return Status::OK();
      }
      return Status::Syntax("unsupported SET TRANSACTION", ParseErrorKind::kSyntax);
  }
  return Status::Syntax("SET TRANSACTION expected", ParseErrorKind::kSyntax);
}

Status ParseStmtTxn(const std::string& head, const std::vector<std::string>& tokens,
                    size_t* pos, SqlStatement* out) {
  if (head == "BEGIN" || head == "START") {
    out->kind = SqlStatementKind::kBegin;
    if (head == "START") {
      if ((*pos) >= tokens.size() || Upper(tokens[*pos]) != "TRANSACTION") {
        return Status::Syntax("START TRANSACTION expected", ParseErrorKind::kSyntax);
      }
      ++(*pos);
    }
    if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "READ") {
      ++(*pos);
      if ((*pos) >= tokens.size() || Upper(tokens[*pos]) != "ONLY") {
        return Status::Syntax("READ ONLY expected", ParseErrorKind::kSyntax);
      }
      ++(*pos);
      out->txn_read_only = true;
    }
    return Status::OK();
  }

  if (head == "COMMIT") {
    out->kind = SqlStatementKind::kCommit;
    return Status::OK();
  }

  if (head == "SAVEPOINT") {
    out->kind = SqlStatementKind::kSavepoint;
    if ((*pos) >= tokens.size()) {
      return Status::Syntax("SAVEPOINT requires name", ParseErrorKind::kSyntax);
    }
    out->savepoint_name = tokens[(*pos)++];
    return Status::OK();
  }

  if (head == "ROLLBACK") {
    if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "WORK") {
      ++(*pos);
    }
    if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "TO") {
      ++(*pos);
      if ((*pos) < tokens.size() && Upper(tokens[*pos]) == "SAVEPOINT") {
        ++(*pos);
      }
      if ((*pos) >= tokens.size()) {
        return Status::Syntax("ROLLBACK TO requires savepoint name", ParseErrorKind::kSyntax);
      }
      out->kind = SqlStatementKind::kRollbackToSavepoint;
      out->savepoint_name = tokens[(*pos)++];
      return Status::OK();
    }
    out->kind = SqlStatementKind::kRollback;
    return Status::OK();
  }

  if (head == "RELEASE") {
    if ((*pos) >= tokens.size() || Upper(tokens[*pos]) != "SAVEPOINT") {
      return Status::Syntax("RELEASE SAVEPOINT expected", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if ((*pos) >= tokens.size()) {
      return Status::Syntax("RELEASE SAVEPOINT requires name", ParseErrorKind::kSyntax);
    }
    out->kind = SqlStatementKind::kReleaseSavepoint;
    out->savepoint_name = tokens[(*pos)++];
    return Status::OK();
  }
  return Status::Syntax("not handled by ParseStmtTxn", ParseErrorKind::kSyntax);
}

}  // namespace detail
}  // namespace heterodb::sql_parse
