#pragma once

#include <cstdint>
#include <string>

namespace ebtree {
namespace sql {

enum class SqlValueKind { kNull, kInteger, kReal, kText, kBlob };

struct SqlValue {
  SqlValueKind kind{SqlValueKind::kNull};
  int64_t i{0};
  double r{0.0};
  std::string text;

  static SqlValue Null();
  static SqlValue Integer(int64_t v);
  static SqlValue Real(double v);
  static SqlValue Text(std::string v);
  static SqlValue FromLegacyString(const std::string& s);

  bool IsNull() const { return kind == SqlValueKind::kNull; }

  // coltype: 'I' integer display, 'R' real, 'T' text, 0 = default text-ish
  std::string ToDisplayString(char coltype = 0) const;
  std::string ToLegacyString() const;
};

}  // namespace sql
}  // namespace ebtree
