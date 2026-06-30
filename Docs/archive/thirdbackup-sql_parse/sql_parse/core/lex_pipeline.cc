#include "sql_parse/core/lex_pipeline.h"

#include <algorithm>

#include "sql_parse/core/parse_context.h"

namespace heterodb::sql_parse {

void LexPipeline::Register(ParseRule rule) {
  steps_.push_back(std::move(rule));
  std::stable_sort(steps_.begin(), steps_.end(),
                   [](const ParseRule& a, const ParseRule& b) {
                     return a.priority > b.priority;
                   });
}

Status LexPipeline::Run(ParseContext* ctx) const {
  if (ctx == nullptr) {
    return Status::InvalidArgument("null parse context");
  }
  for (const ParseRule& step : steps_) {
    if (step.match != nullptr && step.match(*ctx)) {
      if (step.handler == nullptr) {
        return Status::InvalidArgument("lex step handler missing: " + step.name);
      }
      Status s = step.handler(ctx);
      if (!s.ok()) {
        return s;
      }
    }
  }
  return Status::OK();
}

}  // namespace heterodb::sql_parse
