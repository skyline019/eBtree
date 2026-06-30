#include "parse_shared.h"

#include <algorithm>
#include <cctype>

namespace ebtree {
namespace sql {
namespace parse {

std::string Trim(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string Upper(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return s;
}

bool StartsWithCI(const std::string& s, const char* prefix) {
  const std::string u = Upper(s);
  const std::string p = Upper(std::string(prefix));
  return u.rfind(p, 0) == 0;
}

std::string UnquoteToken(const std::string& tok) {
  if (tok.size() >= 2 && tok.front() == '\'' && tok.back() == '\'') {
    return tok.substr(1, tok.size() - 2);
  }
  return tok;
}

bool IsQuotedLiteral(const std::string& tok) {
  return tok.size() >= 2 && tok.front() == '\'';
}

std::optional<uint64_t> ExtractMaxPagesHint(const std::string& sql) {
  const auto pos = sql.find("@max_pages=");
  if (pos == std::string::npos) return std::nullopt;
  size_t i = pos + 11;
  uint64_t n = 0;
  bool any = false;
  while (i < sql.size() && std::isdigit(static_cast<unsigned char>(sql[i]))) {
    any = true;
    n = n * 10 + static_cast<uint64_t>(sql[i] - '0');
    ++i;
  }
  if (!any) return std::nullopt;
  return n;
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
