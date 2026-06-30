#include "sql_parse/stmt/stmt_head_table.h"

namespace heterodb::sql_parse {

namespace {

constexpr StmtHeadRoute kStmtHeadRoutes[] = {
    {"WITH", StmtHeadRouteKind::kQuery},
    {"SELECT", StmtHeadRouteKind::kQuery},
    {"USE", StmtHeadRouteKind::kMeta},
    {"SHOW", StmtHeadRouteKind::kMeta},
    {"DESCRIBE", StmtHeadRouteKind::kMeta},
    {"DESC", StmtHeadRouteKind::kMeta},
    {"EXPLAIN", StmtHeadRouteKind::kMeta},
    {"GRANT", StmtHeadRouteKind::kPriv},
    {"REVOKE", StmtHeadRouteKind::kPriv},
    {"TRUNCATE", StmtHeadRouteKind::kDml},
    {"REPLACE", StmtHeadRouteKind::kDml},
    {"INSERT", StmtHeadRouteKind::kDml},
    {"UPDATE", StmtHeadRouteKind::kDml},
    {"DELETE", StmtHeadRouteKind::kDml},
    {"RENAME", StmtHeadRouteKind::kRename},
    {"CREATE", StmtHeadRouteKind::kDdl},
    {"DROP", StmtHeadRouteKind::kDdl},
    {"ALTER", StmtHeadRouteKind::kDdl},
    {"REINDEX", StmtHeadRouteKind::kDdl},
    {"SET", StmtHeadRouteKind::kSet},
    {"BEGIN", StmtHeadRouteKind::kTxn},
    {"START", StmtHeadRouteKind::kTxn},
    {"COMMIT", StmtHeadRouteKind::kTxn},
    {"SAVEPOINT", StmtHeadRouteKind::kTxn},
    {"ROLLBACK", StmtHeadRouteKind::kTxn},
    {"RELEASE", StmtHeadRouteKind::kTxn},
    {"ANALYZE", StmtHeadRouteKind::kAnalyze},
    {"OPTIMIZE", StmtHeadRouteKind::kOptimize},
    {"FLUSH", StmtHeadRouteKind::kFlush},
};

}  // namespace

const StmtHeadRoute* LookupStmtHeadRoute(std::string_view upper_head) {
  for (const auto& route : kStmtHeadRoutes) {
    if (route.head == upper_head) {
      return &route;
    }
  }
  return nullptr;
}

}  // namespace heterodb::sql_parse
