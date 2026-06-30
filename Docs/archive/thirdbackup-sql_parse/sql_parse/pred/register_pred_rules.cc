#include "sql_parse/bootstrap/parse_module.h"
#include "sql_parse/core/parse_context.h"
#include "sql_parse/core/parse_rule.h"
#include "sql_parse/pred/pred_registry.h"
#include "sql_parse/pred/pred_track.h"
#include "sql_parse/shared/parse_shared.h"
#include "sql_parse/pred/having_parse.h"
#include "sql_parse/pred/where_parse.h"

namespace heterodb::sql_parse {

namespace {

bool MatchWhereKeyword(const ParseContext& ctx) {
  if (ctx.out == nullptr || ctx.cursor.AtEnd()) {
    return false;
  }
  return Upper(ctx.cursor.Peek()) == "WHERE";
}

Status PredWhereClauseHandler(ParseContext* ctx) {
  return pred::ParseWhere(ctx);
}

ParseRule MakeWhereRule() {
  ParseRule rule;
  rule.name = "PredWhereClause";
  rule.priority = 10;
  rule.match = MatchWhereKeyword;
  rule.handler = PredWhereClauseHandler;
  return rule;
}

ParseRule MakeHavingRule() {
  ParseRule rule;
  rule.name = "PredHavingClause";
  rule.priority = 10;
  rule.match = [](const ParseContext& ctx) {
    if (ctx.out == nullptr || ctx.cursor.AtEnd()) {
      return false;
    }
    return Upper(ctx.cursor.Peek()) == "HAVING";
  };
  rule.handler = [](ParseContext* ctx) { return pred::ParseHaving(ctx); };
  return rule;
}

}  // namespace

void RegisterPredRules(PredRegistry* registry) {
  if (registry == nullptr) {
    return;
  }
  registry->rules(PredTrack::kColumn).Register(MakeWhereRule());
  registry->rules(PredTrack::kColumn).Register(MakeHavingRule());
}

}  // namespace heterodb::sql_parse
