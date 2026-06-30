#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common/status.h"
#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {

/** Token stream cursor; replaces raw (tokens, pos) pairs in clause parsers. */
class TokenCursor {
 public:
  TokenCursor() = default;
  explicit TokenCursor(std::vector<std::string> tokens);

  void Reset(std::vector<std::string> tokens);
  const std::vector<std::string>& tokens() const { return tokens_; }
  size_t pos() const { return pos_; }
  size_t& mutable_pos() { return pos_; }
  void set_pos(size_t p) { pos_ = p; }
  bool AtEnd() const { return pos_ >= tokens_.size(); }

  const std::string& Peek(size_t offset = 0) const;
  std::string PeekUpper(size_t offset = 0) const;
  std::string HeadUpper() const;

  Status Consume(std::string* out);
  Status ExpectKeyword(const std::string& keyword);
  Status ExpectKeywordCI(const char* keyword);

  /** Rebuild SQL from current position (subquery / view bodies). */
  std::string RestSql() const;

 private:
  std::vector<std::string> tokens_;
  size_t pos_{0};
};

}  // namespace heterodb::sql_parse
