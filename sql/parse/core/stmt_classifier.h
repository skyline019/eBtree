#pragma once

#include <string>

namespace ebtree {
namespace sql {
namespace parse {

enum class StmtClass {
  kOltp,
  kBeginTxn,
  kCommit,
  kRollback,
  kSavepoint,
  kWithCte,
  kSetOp,
  kWindow,
  kShow,
  kSet,
  kGrant,
  kExplain,
  kPrepare,
  kExecute,
  kPragma,
  kCreateView,
  kDropView,
  kCreateTrigger,
  kDropTrigger,
  kReindex,
  kUnknown,
};

StmtClass ClassifyStatement(const std::string& sql);
const char* StmtClassName(StmtClass c);

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
