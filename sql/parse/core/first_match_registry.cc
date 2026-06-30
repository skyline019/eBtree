#include "first_match_registry.h"

#include <algorithm>

#include "sql/parse/core/parse_context.h"

namespace ebtree {
namespace sql {
namespace parse {

void FirstMatchRegistry::Register(ParseRule rule) {
  rules_.push_back(std::move(rule));
  std::stable_sort(rules_.begin(), rules_.end(),
                   [](const ParseRule& a, const ParseRule& b) {
                     return a.priority > b.priority;
                   });
}

Status FirstMatchRegistry::Dispatch(ParseContext* ctx) const {
  if (!ctx || !ctx->out) return Status::InvalidArgument("invalid parse context");
  for (const auto& rule : rules_) {
    if (rule.match && rule.match(*ctx)) {
      return rule.handler ? rule.handler(ctx) : Status::Ok();
    }
  }
  return Status::InvalidArgument("unsupported sql statement");
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
