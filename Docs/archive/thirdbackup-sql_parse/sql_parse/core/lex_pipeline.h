#pragma once

#include <vector>

#include "common/status.h"
#include "sql_parse/core/parse_rule.h"

namespace heterodb::sql_parse {

class ParseContext;

/** Sequential pipeline: every matching rule runs in priority order (Lex phase). */
class LexPipeline {
 public:
  void Register(ParseRule rule);
  Status Run(ParseContext* ctx) const;
  const std::vector<ParseRule>& steps() const { return steps_; }

 private:
  std::vector<ParseRule> steps_;
};

}  // namespace heterodb::sql_parse
