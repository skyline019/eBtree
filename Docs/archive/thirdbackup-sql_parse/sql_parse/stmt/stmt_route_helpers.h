#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#include "sql_parse/core/parse_context.h"
#include "sql_parse/core/parse_rule.h"

namespace heterodb::sql_parse {

inline ParseMatchFn MatchHead(const char* keyword) {
  const std::string kw = keyword;
  return [kw](const ParseContext& ctx) { return ctx.HeadUpper() == kw; };
}

inline ParseMatchFn MatchHeadAny(
    std::initializer_list<const char*> keywords) {
  std::vector<std::string> kws;
  kws.reserve(keywords.size());
  for (const char* k : keywords) {
    kws.emplace_back(k);
  }
  return [kws = std::move(kws)](const ParseContext& ctx) {
    const std::string head = ctx.HeadUpper();
    for (const auto& kw : kws) {
      if (head == kw) {
        return true;
      }
    }
    return false;
  };
}

inline ParseRule MakeRoute(const char* name, int priority, ParseMatchFn match,
                           ParseHandlerFn handler) {
  ParseRule rule;
  rule.name = name;
  rule.priority = priority;
  rule.match = std::move(match);
  rule.handler = std::move(handler);
  return rule;
}

}  // namespace heterodb::sql_parse
