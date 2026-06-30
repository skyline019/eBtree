#pragma once

#include <vector>

#include "ebtree/common/status.h"
#include "sql/parse/core/parse_rule.h"

namespace ebtree {
namespace sql {
namespace parse {

class FirstMatchRegistry {
 public:
  void Register(ParseRule rule);
  Status Dispatch(ParseContext* ctx) const;

 private:
  std::vector<ParseRule> rules_;
};

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
