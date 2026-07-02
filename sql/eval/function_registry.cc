#include "function_registry.h"

#include <cstdio>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "like_match.h"

namespace ebtree {
namespace sql {

namespace {

int64_t AsInt(const std::string& s) {
  try {
    return std::stoll(s);
  } catch (...) {
    return 0;
  }
}

std::string UpperStr(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string LowerStr(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string TrimStr(const std::string& s) {
  size_t start = 0;
  while (start < s.size() &&
         std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

SqlValue CastValue(const SqlValue& v, const std::string& type_name) {
  const std::string t = UpperStr(type_name);
  if (t.find("INT") != std::string::npos) {
    if (v.IsNull()) return SqlValue::Null();
    return SqlValue::Integer(static_cast<int64_t>(std::strtoll(
        v.ToLegacyString().c_str(), nullptr, 10)));
  }
  if (t.find("REAL") != std::string::npos || t.find("FLOA") != std::string::npos ||
      t.find("DOUB") != std::string::npos) {
    if (v.IsNull()) return SqlValue::Null();
    return SqlValue::Real(std::strtod(v.ToLegacyString().c_str(), nullptr));
  }
  if (t.find("TEXT") != std::string::npos || t.find("CHAR") != std::string::npos) {
    if (v.IsNull()) return SqlValue::Null();
    return SqlValue::Text(v.ToLegacyString());
  }
  return v;
}

}  // namespace

SqlValue FunctionRegistry::Eval(const std::string& name, const ExprNode& node,
                                const RowMap& row, const ExprEval* eval) {
  if (!eval) return SqlValue::Null();
  const std::string fn = UpperStr(name);

  if (fn == "CAST" && node.children.size() >= 2) {
    const SqlValue v = eval->EvalValue(*node.children[0], row);
    return CastValue(v, eval->EvalScalar(*node.children[1], row));
  }
  if (fn == "NULLIF" && node.children.size() >= 2) {
    const SqlValue a = eval->EvalValue(*node.children[0], row);
    const SqlValue b = eval->EvalValue(*node.children[1], row);
    if (a.IsNull() || b.IsNull()) return a.IsNull() ? b : a;
    if (a.ToLegacyString() == b.ToLegacyString()) return SqlValue::Null();
    return a;
  }
  if (fn == "IFNULL" && node.children.size() >= 2) {
    const SqlValue a = eval->EvalValue(*node.children[0], row);
    if (!a.IsNull()) return a;
    return eval->EvalValue(*node.children[1], row);
  }
  if (fn == "IIF" && node.children.size() >= 3) {
    return eval->EvalBool(*node.children[0], row)
               ? eval->EvalValue(*node.children[1], row)
               : eval->EvalValue(*node.children[2], row);
  }
  if (fn == "COALESCE") {
    for (const auto& c : node.children) {
      const SqlValue v = eval->EvalValue(*c, row);
      if (!v.IsNull()) return v;
    }
    return SqlValue::Null();
  }
  if (fn == "LENGTH" && !node.children.empty()) {
    return SqlValue::Integer(static_cast<int64_t>(
        eval->EvalScalar(*node.children[0], row).size()));
  }
  if (fn == "SUBSTR" && node.children.size() >= 2) {
    const std::string s = eval->EvalScalar(*node.children[0], row);
    const int64_t start = AsInt(eval->EvalScalar(*node.children[1], row));
    const size_t pos = start < 1 ? 0 : static_cast<size_t>(start - 1);
    if (pos >= s.size()) return SqlValue::Null();
    if (node.children.size() >= 3) {
      const int64_t len = AsInt(eval->EvalScalar(*node.children[2], row));
      return SqlValue::Text(s.substr(pos, static_cast<size_t>(len)));
    }
    return SqlValue::Text(s.substr(pos));
  }
  if (fn == "TYPEOF" && !node.children.empty()) {
    const SqlValue v = eval->EvalValue(*node.children[0], row);
    if (v.IsNull()) return SqlValue::Text("null");
    if (v.kind == SqlValueKind::kInteger) return SqlValue::Text("integer");
    if (v.kind == SqlValueKind::kReal) return SqlValue::Text("real");
    return SqlValue::Text("text");
  }
  if (fn == "ABS" && !node.children.empty()) {
    const SqlValue v = eval->EvalValue(*node.children[0], row);
    if (v.IsNull()) return SqlValue::Null();
    return SqlValue::Integer(std::llabs(AsInt(v.ToLegacyString())));
  }
  if (fn == "UPPER" && !node.children.empty()) {
    const SqlValue v = eval->EvalValue(*node.children[0], row);
    if (v.IsNull()) return SqlValue::Null();
    return SqlValue::Text(UpperStr(v.ToLegacyString()));
  }
  if (fn == "LOWER" && !node.children.empty()) {
    const SqlValue v = eval->EvalValue(*node.children[0], row);
    if (v.IsNull()) return SqlValue::Null();
    return SqlValue::Text(LowerStr(v.ToLegacyString()));
  }
  if (fn == "TRIM" && !node.children.empty()) {
    const SqlValue v = eval->EvalValue(*node.children[0], row);
    if (v.IsNull()) return SqlValue::Null();
    return SqlValue::Text(TrimStr(v.ToLegacyString()));
  }
  if (fn == "QUOTE" && !node.children.empty()) {
    const SqlValue v = eval->EvalValue(*node.children[0], row);
    if (v.IsNull()) return SqlValue::Text("NULL");
    return SqlValue::Text("'" + v.ToLegacyString() + "'");
  }
  if (fn == "HEX" && !node.children.empty()) {
    const std::string s = eval->EvalScalar(*node.children[0], row);
    std::string out;
    for (unsigned char c : s) {
      char buf[3];
      std::snprintf(buf, sizeof(buf), "%02X", c);
      out += buf;
    }
    return SqlValue::Text(out);
  }
  if ((fn == "MIN" || fn == "MAX") && node.children.size() >= 2) {
    SqlValue best = eval->EvalValue(*node.children[0], row);
    for (size_t i = 1; i < node.children.size(); ++i) {
      const SqlValue v = eval->EvalValue(*node.children[i], row);
      if (v.IsNull()) continue;
      if (best.IsNull()) {
        best = v;
        continue;
      }
      const bool lt = best.ToLegacyString() < v.ToLegacyString();
      if ((fn == "MIN" && lt) || (fn == "MAX" && !lt)) best = v;
    }
    return best;
  }
  return SqlValue::Null();
}

}  // namespace sql
}  // namespace ebtree
