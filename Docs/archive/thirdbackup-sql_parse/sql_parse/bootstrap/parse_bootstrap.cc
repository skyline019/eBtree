#include "sql_parse/bootstrap/parse_bootstrap.h"

#include <memory>

#include "concept/schema/schema.h"
#include "sql_parse/bootstrap/parse_module.h"
#include "sql_parse/core/lex_pipeline.h"
#include "sql_parse/core/parse_context.h"
#include "sql_parse/expr/expr_registry.h"
#include "sql_parse/pred/pred_registry.h"
#include "sql_parse/stmt/parse_common_api.h"
#include "sql_parse/stmt/stmt_registry.h"

namespace heterodb::sql_parse {

class ParseBootstrap::Impl {
 public:
  LexPipeline lex;
  StmtRegistry stmt;
};

ParseBootstrap::ParseBootstrap() : impl_(new Impl()) {}

ParseBootstrap::~ParseBootstrap() { delete impl_; }

ParseBootstrap& ParseBootstrap::Global() {
  static ParseBootstrap instance;
  return instance;
}

void ParseBootstrap::InstallAll() {
  std::call_once(install_once_, [this]() {
    RegisterLexRules(&impl_->lex);
    RegisterExprPlugins(&impl_->stmt.expr());
    RegisterPredRules(&impl_->stmt.pred());
    RegisterStmtRoutes(&impl_->stmt);
    InstallRouterStmtFallback(&impl_->stmt.routes());
    installed_ = true;
  });
}

Status ParseBootstrap::Parse(const std::string& sql, SqlStatement* out,
                             const std::string& current_database) {
  InstallAll();
  if (out == nullptr) {
    return Status::InvalidArgument("null output");
  }
  *out = SqlStatement{};

  ParseContext ctx;
  ctx.raw_sql = sql;
  ctx.current_database =
      current_database.empty() ? std::string(kDefaultDatabaseName)
                               : current_database;
  ctx.out = out;

  Status s = impl_->lex.Run(&ctx);
  if (!s.ok()) {
    return s;
  }
  detail::ParseSessionScope session(
      ctx.current_database.empty() ? std::string(kDefaultDatabaseName)
                                   : ctx.current_database,
      ctx.catalog_bridge);
  return impl_->stmt.routes().Dispatch(&ctx);
}

}  // namespace heterodb::sql_parse
