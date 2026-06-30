#include "stmt_classifier.h"

#include "sql/parse/shared/parse_shared.h"

namespace ebtree {
namespace sql {
namespace parse {

StmtClass ClassifyStatement(const std::string& sql) {
  const std::string u = Upper(Trim(sql));
  if (StartsWithCI(u, "BEGIN") || StartsWithCI(u, "START TRANSACTION")) {
    return StmtClass::kBeginTxn;
  }
  if (StartsWithCI(u, "COMMIT")) return StmtClass::kCommit;
  if (StartsWithCI(u, "ROLLBACK")) return StmtClass::kRollback;
  if (StartsWithCI(u, "SAVEPOINT") || StartsWithCI(u, "RELEASE SAVEPOINT")) {
    return StmtClass::kSavepoint;
  }
  if (StartsWithCI(u, "WITH")) return StmtClass::kWithCte;
  if (u.find(" OVER(") != std::string::npos || u.find(" OVER (") != std::string::npos ||
      u.find("ROW_NUMBER(") != std::string::npos || u.find("RANK(") != std::string::npos) {
    return StmtClass::kWindow;
  }
  if (u.find(" UNION ") != std::string::npos ||
      u.find(" INTERSECT ") != std::string::npos ||
      u.find(" EXCEPT ") != std::string::npos) {
    return StmtClass::kSetOp;
  }
  if (StartsWithCI(u, "SHOW") || StartsWithCI(u, "DESC") ||
      StartsWithCI(u, "DESCRIBE")) {
    return StmtClass::kShow;
  }
  if (StartsWithCI(u, "SET ")) return StmtClass::kSet;
  if (StartsWithCI(u, "GRANT") || StartsWithCI(u, "REVOKE")) return StmtClass::kGrant;
  if (StartsWithCI(u, "EXPLAIN")) return StmtClass::kExplain;
  if (StartsWithCI(u, "PREPARE")) return StmtClass::kPrepare;
  if (StartsWithCI(u, "EXECUTE")) return StmtClass::kExecute;
  if (StartsWithCI(u, "PRAGMA")) return StmtClass::kPragma;
  if (StartsWithCI(u, "REINDEX")) return StmtClass::kReindex;
  if (StartsWithCI(u, "CREATE VIEW")) return StmtClass::kCreateView;
  if (StartsWithCI(u, "CREATE TEMP VIEW") || StartsWithCI(u, "CREATE TEMPORARY VIEW")) {
    return StmtClass::kCreateView;
  }
  if (StartsWithCI(u, "CREATE TRIGGER")) return StmtClass::kCreateTrigger;
  if (StartsWithCI(u, "DROP TRIGGER")) return StmtClass::kDropTrigger;
  if (StartsWithCI(u, "DROP VIEW")) return StmtClass::kDropView;
  if (StartsWithCI(u, "SELECT") || StartsWithCI(u, "INSERT") ||
      StartsWithCI(u, "UPDATE") || StartsWithCI(u, "DELETE") ||
      StartsWithCI(u, "CREATE") || StartsWithCI(u, "DROP") ||
      StartsWithCI(u, "ALTER") || StartsWithCI(u, "OPEN")) {
    return StmtClass::kOltp;
  }
  return StmtClass::kUnknown;
}

const char* StmtClassName(StmtClass c) {
  switch (c) {
    case StmtClass::kBeginTxn: return "BEGIN";
    case StmtClass::kCommit: return "COMMIT";
    case StmtClass::kRollback: return "ROLLBACK";
    case StmtClass::kSavepoint: return "SAVEPOINT";
    case StmtClass::kWithCte: return "WITH";
    case StmtClass::kSetOp: return "SET_OP";
    case StmtClass::kWindow: return "WINDOW";
    case StmtClass::kShow: return "SHOW";
    case StmtClass::kSet: return "SET";
    case StmtClass::kGrant: return "GRANT";
    case StmtClass::kExplain: return "EXPLAIN";
    case StmtClass::kPrepare: return "PREPARE";
    case StmtClass::kExecute: return "EXECUTE";
    case StmtClass::kPragma: return "PRAGMA";
    case StmtClass::kCreateView: return "CREATE_VIEW";
    case StmtClass::kDropView: return "DROP_VIEW";
    case StmtClass::kCreateTrigger: return "CREATE_TRIGGER";
    case StmtClass::kDropTrigger: return "DROP_TRIGGER";
    case StmtClass::kReindex: return "REINDEX";
    case StmtClass::kOltp: return "OLTP";
    default: return "UNKNOWN";
  }
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
