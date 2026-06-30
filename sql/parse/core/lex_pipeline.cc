#include "lex_pipeline.h"

#include <algorithm>
#include <cctype>

#include "sql/parse/core/parse_context.h"

namespace ebtree {
namespace sql {
namespace parse {

namespace {

bool IsIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

void PushToken(std::vector<std::string>* out, std::string* cur) {
  if (!cur->empty()) {
    out->push_back(*cur);
    cur->clear();
  }
}

}  // namespace

std::vector<std::string> TokenizeSql(const std::string& sql) {
  std::vector<std::string> tokens;
  std::string cur;
  for (size_t i = 0; i < sql.size(); ++i) {
    const char c = sql[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      PushToken(&tokens, &cur);
      continue;
    }
    if (c == '\'' ) {
      PushToken(&tokens, &cur);
      if (!tokens.empty()) {
        const std::string& prev = tokens.back();
        if (prev.size() == 1 && (prev[0] == 'x' || prev[0] == 'X')) {
          tokens.pop_back();
          std::string blob = prev;
          blob.push_back('\'');
          ++i;
          while (i < sql.size() && sql[i] != '\'') {
            blob.push_back(sql[i++]);
          }
          if (i < sql.size()) blob.push_back('\'');
          tokens.push_back(blob);
          continue;
        }
      }
      std::string lit;
      lit.push_back('\'');
      ++i;
      while (i < sql.size()) {
        if (sql[i] == '\'') {
          if (i + 1 < sql.size() && sql[i + 1] == '\'') {
            lit.push_back('\'');
            i += 2;
            continue;
          }
          lit.push_back('\'');
          break;
        }
        lit.push_back(sql[i++]);
      }
      tokens.push_back(lit);
      continue;
    }
    if (c == '.') {
      if (!cur.empty() && i + 1 < sql.size() &&
          std::isdigit(static_cast<unsigned char>(sql[i + 1]))) {
        cur.push_back(c);
        continue;
      }
      PushToken(&tokens, &cur);
      tokens.push_back(".");
      continue;
    }
    if (c == '(' || c == ')' || c == ',' || c == ';' ||
        c == '*' || c == '=' || c == '<' || c == '>' || c == '+' ||
        c == '-' || c == '/') {
      PushToken(&tokens, &cur);
      if ((c == '<' || c == '>' || c == '!') && i + 1 < sql.size() &&
          sql[i + 1] == '=') {
        tokens.push_back(sql.substr(i, 2));
        ++i;
      } else if (c == '!' && i + 1 < sql.size() && sql[i + 1] == '=') {
        tokens.push_back("!=");
        ++i;
      } else if (c == '<' && i + 1 < sql.size() && sql[i + 1] == '>') {
        tokens.push_back("<>");
        ++i;
      } else {
        tokens.push_back(std::string(1, c));
      }
      continue;
    }
    cur.push_back(c);
  }
  PushToken(&tokens, &cur);
  return tokens;
}

void LexPipeline::Register(ParseRule rule) {
  steps_.push_back(std::move(rule));
  std::stable_sort(steps_.begin(), steps_.end(),
                   [](const ParseRule& a, const ParseRule& b) {
                     return a.priority > b.priority;
                   });
}

Status LexPipeline::Run(ParseContext* ctx) const {
  if (!ctx) return Status::InvalidArgument("null context");
  for (const auto& step : steps_) {
    if (step.match && step.match(*ctx)) {
      return step.handler ? step.handler(ctx) : Status::Ok();
    }
  }
  return Status::InvalidArgument("lex pipeline: no rule matched");
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
