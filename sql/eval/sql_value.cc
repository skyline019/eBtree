#include "sql_value.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ebtree {
namespace sql {

namespace {

bool IsIntegerText(const std::string& s) {
  if (s.empty()) return false;
  size_t i = 0;
  if (s[0] == '-' || s[0] == '+') {
    if (s.size() == 1) return false;
    i = 1;
  }
  for (; i < s.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
  }
  return true;
}

bool IsRealText(const std::string& s) {
  if (s.empty()) return false;
  char* end = nullptr;
  std::strtod(s.c_str(), &end);
  return end != s.c_str() && *end == '\0';
}

}  // namespace

SqlValue SqlValue::Null() { return SqlValue{}; }

SqlValue SqlValue::Integer(int64_t v) {
  SqlValue out;
  out.kind = SqlValueKind::kInteger;
  out.i = v;
  out.r = static_cast<double>(v);
  return out;
}

SqlValue SqlValue::Real(double v) {
  SqlValue out;
  out.kind = SqlValueKind::kReal;
  out.r = v;
  out.i = static_cast<int64_t>(v);
  return out;
}

SqlValue SqlValue::Text(std::string v) {
  SqlValue out;
  out.kind = SqlValueKind::kText;
  out.text = std::move(v);
  return out;
}

SqlValue SqlValue::FromLegacyString(const std::string& s) {
  if (s.empty()) return Null();
  if (IsIntegerText(s)) {
    try {
      return Integer(std::stoll(s));
    } catch (...) {
      return Text(s);
    }
  }
  if (IsRealText(s)) {
    char* end = nullptr;
    const double d = std::strtod(s.c_str(), &end);
    if (end && *end == '\0') return Real(d);
  }
  return Text(s);
}

std::string SqlValue::ToDisplayString(char coltype) const {
  if (IsNull()) return "";
  if (coltype == 'I') {
    if (kind == SqlValueKind::kInteger) return std::to_string(i);
    if (kind == SqlValueKind::kReal) return std::to_string(static_cast<int64_t>(r));
    if (kind == SqlValueKind::kText && IsIntegerText(text)) return text;
    if (kind == SqlValueKind::kText && IsRealText(text)) {
      char* end = nullptr;
      const double d = std::strtod(text.c_str(), &end);
      if (end && *end == '\0') return std::to_string(static_cast<int64_t>(d));
    }
    return "0";
  }
  if (coltype == 'R') {
    double dv = r;
    if (kind == SqlValueKind::kInteger) dv = static_cast<double>(i);
    else if (kind == SqlValueKind::kText && IsRealText(text)) {
      dv = std::strtod(text.c_str(), nullptr);
    } else if (kind == SqlValueKind::kText) return text;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", dv);
    return std::string(buf);
  }
  return ToLegacyString();
}

std::string SqlValue::ToLegacyString() const {
  if (IsNull()) return "";
  switch (kind) {
    case SqlValueKind::kInteger:
      return std::to_string(i);
    case SqlValueKind::kReal: {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%g", r);
      return std::string(buf);
    }
    case SqlValueKind::kText:
      return text;
    case SqlValueKind::kBlob:
      return text;
    default:
      return "";
  }
}

}  // namespace sql
}  // namespace ebtree
