#pragma once

#include <string>
#include <vector>

namespace heterodb::sql_parse {

struct ParseDiagnostic {
  std::string rule_name;
  size_t token_index{0};
  std::string message;
};

class DiagnosticSink {
 public:
  void Add(ParseDiagnostic d) { entries_.push_back(std::move(d)); }
  const std::vector<ParseDiagnostic>& entries() const { return entries_; }
  void Clear() { entries_.clear(); }

 private:
  std::vector<ParseDiagnostic> entries_;
};

}  // namespace heterodb::sql_parse
