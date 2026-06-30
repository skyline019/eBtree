#include "sql_parse/core/parse_registry.h"

#include "common/parse_error.h"

#include <algorithm>

#include "common/status.h"
#include "sql_parse/core/parse_context.h"

namespace heterodb::sql_parse {

void FirstMatchRegistry::Register(ParseRule rule) {
  rules_.push_back(std::move(rule));
  std::stable_sort(rules_.begin(), rules_.end(),
                   [](const ParseRule& a, const ParseRule& b) {
                     return a.priority > b.priority;
                   });
}

Status FirstMatchRegistry::Dispatch(ParseContext* ctx) const {
  if (ctx == nullptr) {
    return Status::InvalidArgument("null parse context");
  }
  for (const ParseRule& rule : rules_) {
    if (rule.match != nullptr && rule.match(*ctx)) {
      if (rule.handler == nullptr) {
        return Status::InvalidArgument("parse rule handler missing: " + rule.name);
      }
      Status s = rule.handler(ctx);
      if (!s.ok()) {
        ctx->EmitDiagnostic(rule.name, s.message());
      }
      return s;
    }
  }
  ctx->EmitDiagnostic("StmtRegistry", "no parse rule matched");
  return Status::Syntax("no parse rule matched", ParseErrorKind::kSyntax);
}

}  // namespace heterodb::sql_parse
