#pragma once

#include <string>
#include <string_view>

namespace heterodb::sql_parse {

enum class StmtHeadRouteKind {
  kQuery,
  kMeta,
  kPriv,
  kDml,
  kRename,
  kDdl,
  kSet,
  kTxn,
  kAnalyze,
  kOptimize,
  kFlush,
  kUnsupported,
};

struct StmtHeadRoute {
  std::string_view head;
  StmtHeadRouteKind kind;
};

// SSOT for statement-leading keyword routing (Phase 100).
const StmtHeadRoute* LookupStmtHeadRoute(std::string_view upper_head);

}  // namespace heterodb::sql_parse
