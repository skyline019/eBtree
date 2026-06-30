#include "native_parser.h"

#include "sql/parse/core/first_match_registry.h"
#include "sql/parse/core/lex_pipeline.h"
#include "sql/parse/core/parse_context.h"
#include "sql/parse/core/stmt_classifier.h"
#include "sql/parse/native/stmt_handlers.h"
#include "sql/parse/native/advanced_parse.h"

namespace ebtree {
namespace sql {
namespace parse {

namespace {

struct BootstrapState {
  LexPipeline lex;
  FirstMatchRegistry stmt;
  bool installed{false};
};

BootstrapState& State() {
  static BootstrapState s;
  return s;
}

void InstallAll() {
  auto& s = State();
  if (s.installed) return;
  s.lex.Register(
      {"tokenize", 1000, [](const ParseContext&) { return true; },
       [](ParseContext* ctx) {
         ctx->ResetTokens(TokenizeSql(ctx->raw_sql));
         return Status::Ok();
       }});
  InstallNativeStmtRules(&s.stmt);
  s.installed = true;
}

QueryStmtKind MapParseOnlyQueryKind(StmtClass cls) {
  switch (cls) {
    case StmtClass::kBeginTxn: return QueryStmtKind::kBeginTxn;
    case StmtClass::kCommit: return QueryStmtKind::kCommit;
    case StmtClass::kRollback: return QueryStmtKind::kRollback;
    case StmtClass::kSavepoint: return QueryStmtKind::kSavepoint;
    case StmtClass::kWithCte: return QueryStmtKind::kWithCte;
    case StmtClass::kSetOp: return QueryStmtKind::kSetOp;
    case StmtClass::kWindow: return QueryStmtKind::kWindowSelect;
    case StmtClass::kShow: return QueryStmtKind::kShow;
    case StmtClass::kSet: return QueryStmtKind::kSet;
    case StmtClass::kGrant: return QueryStmtKind::kGrant;
    case StmtClass::kExplain: return QueryStmtKind::kExplain;
    case StmtClass::kPrepare: return QueryStmtKind::kPrepare;
    case StmtClass::kExecute: return QueryStmtKind::kExecute;
    case StmtClass::kPragma: return QueryStmtKind::kPragma;
    case StmtClass::kCreateView: return QueryStmtKind::kCreateView;
    case StmtClass::kDropView: return QueryStmtKind::kDropView;
    case StmtClass::kCreateTrigger: return QueryStmtKind::kCreateTrigger;
    case StmtClass::kDropTrigger: return QueryStmtKind::kDropTrigger;
    default: return QueryStmtKind::kUnknown;
  }
}

}  // namespace

Status NativeParser::Parse(const std::string& sql, QueryStatement* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  InstallAll();
  *out = QueryStatement{};
  out->raw_sql = sql;

  const StmtClass cls = ClassifyStatement(sql);
  if (cls == StmtClass::kWithCte) {
    return ParseCteQuery(sql, out);
  }
  if (cls == StmtClass::kSetOp) {
    return ParseSetOpQuery(sql, out);
  }
  if (cls == StmtClass::kWindow) {
    return ParseWindowQuery(sql, out);
  }
  if (cls == StmtClass::kPrepare) {
    return ParsePrepareQuery(sql, out);
  }
  if (cls == StmtClass::kExecute) {
    return ParseExecuteQuery(sql, out);
  }
  if (cls == StmtClass::kPragma) {
    out->kind = QueryStmtKind::kPragma;
    return Status::Ok();
  }
  if (cls == StmtClass::kCreateView) {
    return ParseCreateViewQuery(sql, out);
  }
  if (cls == StmtClass::kDropView) {
    return ParseDropViewQuery(sql, out);
  }
  if (cls == StmtClass::kCreateTrigger) {
    return ParseCreateTriggerQuery(sql, out);
  }
  if (cls == StmtClass::kDropTrigger) {
    return ParseDropTriggerQuery(sql, out);
  }
  if (cls == StmtClass::kReindex) {
    return ParseReindexQuery(sql, out);
  }
  if (cls != StmtClass::kOltp && cls != StmtClass::kUnknown) {
    out->kind = MapParseOnlyQueryKind(cls);
    return Status::Ok();
  }

  ParseContext ctx;
  ctx.raw_sql = sql;
  ctx.out = out;
  const Status ls = State().lex.Run(&ctx);
  if (!ls.ok()) return ls;
  return State().stmt.Dispatch(&ctx);
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
