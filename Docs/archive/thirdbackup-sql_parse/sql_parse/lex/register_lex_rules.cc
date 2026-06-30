#include "sql_parse/bootstrap/parse_module.h"

#include "common/parse_error.h"

#include "sql_parse/core/lex_pipeline.h"
#include "sql_parse/core/parse_context.h"
#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {

namespace {

bool MatchAlways(const ParseContext&) { return true; }

Status NormalizeStep(ParseContext* ctx) {
  if (SqlInputNeedsNormalization(ctx->raw_sql)) {
    ctx->normalized_sql = NormalizeSqlInput(ctx->raw_sql);
  } else {
    ctx->normalized_sql = ctx->raw_sql;
  }
  return Status::OK();
}

Status TokenizeStep(ParseContext* ctx) {
  ctx->cursor.Reset(SplitTokens(ctx->normalized_sql));
  if (ctx->cursor.AtEnd()) {
    return Status::Syntax("empty sql", ParseErrorKind::kSyntax);
  }
  return Status::OK();
}

}  // namespace

void RegisterLexRules(LexPipeline* pipeline) {
  if (pipeline == nullptr) {
    return;
  }
  pipeline->Register({"LexNormalize", 100, MatchAlways, NormalizeStep});
  pipeline->Register({"LexTokenize", 90, MatchAlways, TokenizeStep});
}

}  // namespace heterodb::sql_parse
