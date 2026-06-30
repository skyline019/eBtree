#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {
namespace parse {

class ParseContext;

using ParseMatchFn = std::function<bool(const ParseContext&)>;
using ParseHandlerFn = std::function<Status(ParseContext*)>;

struct ParseRule {
  std::string name;
  int priority{0};
  ParseMatchFn match;
  ParseHandlerFn handler;
};

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
