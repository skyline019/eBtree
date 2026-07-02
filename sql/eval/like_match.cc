#include "like_match.h"

namespace ebtree {
namespace sql {

namespace {

bool LikeMatchImpl(const std::string& text, const std::string& pattern, size_t ti,
                   size_t pi, char escape) {
  while (pi < pattern.size()) {
    char p = pattern[pi];
    if (p == escape && pi + 1 < pattern.size()) {
      if (ti >= text.size() || text[ti] != pattern[pi + 1]) return false;
      ++ti;
      pi += 2;
      continue;
    }
    if (p == '%') {
      if (pi + 1 == pattern.size()) return true;
      for (size_t k = ti; k <= text.size(); ++k) {
        if (LikeMatchImpl(text, pattern, k, pi + 1, escape)) return true;
      }
      return false;
    }
    if (p == '_') {
      if (ti >= text.size()) return false;
      ++ti;
      ++pi;
      continue;
    }
    if (ti >= text.size() || text[ti] != p) return false;
    ++ti;
    ++pi;
  }
  return ti == text.size();
}

bool GlobMatchImpl(const std::string& text, const std::string& pattern, size_t ti,
                   size_t pi) {
  while (pi < pattern.size()) {
    const char p = pattern[pi];
    if (p == '*') {
      if (pi + 1 == pattern.size()) return true;
      for (size_t k = ti; k <= text.size(); ++k) {
        if (GlobMatchImpl(text, pattern, k, pi + 1)) return true;
      }
      return false;
    }
    if (p == '?') {
      if (ti >= text.size()) return false;
      ++ti;
      ++pi;
      continue;
    }
    if (p == '[') {
      const size_t close = pattern.find(']', pi + 1);
      if (close == std::string::npos || ti >= text.size()) return false;
      const std::string set = pattern.substr(pi + 1, close - pi - 1);
      if (set.find(text[ti]) == std::string::npos) return false;
      ++ti;
      pi = close + 1;
      continue;
    }
    if (ti >= text.size() || text[ti] != p) return false;
    ++ti;
    ++pi;
  }
  return ti == text.size();
}

}  // namespace

bool SqlLikeMatch(const std::string& text, const std::string& pattern,
                  char escape) {
  return LikeMatchImpl(text, pattern, 0, 0, escape);
}

bool SqlGlobMatch(const std::string& text, const std::string& pattern) {
  return GlobMatchImpl(text, pattern, 0, 0);
}

}  // namespace sql
}  // namespace ebtree
