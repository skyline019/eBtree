#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {
namespace parse {

class TokenCursor {
 public:
  TokenCursor() = default;
  explicit TokenCursor(std::vector<std::string> tokens);

  void Reset(std::vector<std::string> tokens);
  const std::vector<std::string>& tokens() const { return tokens_; }
  size_t pos() const { return pos_; }
  void SetPos(size_t pos) { pos_ = pos; }
  bool AtEnd() const { return pos_ >= tokens_.size(); }

  const std::string& Peek(size_t offset = 0) const;
  std::string PeekUpper(size_t offset = 0) const;

  Status Consume(std::string* out);
  Status ExpectKeywordCI(const char* keyword);

  std::string RestSql() const;

 private:
  std::vector<std::string> tokens_;
  size_t pos_{0};
};

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
