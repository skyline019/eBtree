#pragma once

#include <vector>

#include "ebtree/common/status.h"
#include "sql/parse/core/parse_rule.h"

namespace ebtree {
namespace sql {
namespace parse {

class ParseContext;

class LexPipeline {
 public:
  void Register(ParseRule rule);
  Status Run(ParseContext* ctx) const;

 private:
  std::vector<ParseRule> steps_;
};

std::vector<std::string> TokenizeSql(const std::string& sql);

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
