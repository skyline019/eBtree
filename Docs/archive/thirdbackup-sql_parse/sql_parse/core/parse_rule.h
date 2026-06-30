#pragma once

#include <functional>
#include <string>

#include "common/status.h"

namespace heterodb::sql_parse {

class ParseContext;

using ParseMatchFn = std::function<bool(const ParseContext&)>;
using ParseHandlerFn = std::function<Status(ParseContext*)>;

/** One parse rule: first matching rule (by priority) wins in FirstMatchRegistry. */
struct ParseRule {
  std::string name;
  int priority{0};
  ParseMatchFn match;
  ParseHandlerFn handler;
};

}  // namespace heterodb::sql_parse
