#pragma once

#include <vector>

#include "common/status.h"
#include "sql_parse/core/parse_rule.h"

namespace heterodb::sql_parse {

/** First-match-wins registry (Stmt routes, Pred plugins). */
class FirstMatchRegistry {
 public:
  void Register(ParseRule rule);
  Status Dispatch(ParseContext* ctx) const;
  const std::vector<ParseRule>& rules() const { return rules_; }

 private:
  std::vector<ParseRule> rules_;
};

}  // namespace heterodb::sql_parse
