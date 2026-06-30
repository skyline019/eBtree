#include "sql_parse/core/token_cursor.h"

#include "common/parse_error.h"

#include <sstream>

namespace heterodb::sql_parse {

namespace {

const std::string kEmpty;

}  // namespace

TokenCursor::TokenCursor(std::vector<std::string> tokens)
    : tokens_(std::move(tokens)) {}

void TokenCursor::Reset(std::vector<std::string> tokens) {
  tokens_ = std::move(tokens);
  pos_ = 0;
}

const std::string& TokenCursor::Peek(size_t offset) const {
  const size_t idx = pos_ + offset;
  if (idx >= tokens_.size()) {
    return kEmpty;
  }
  return tokens_[idx];
}

std::string TokenCursor::PeekUpper(size_t offset) const {
  return Upper(Peek(offset));
}

std::string TokenCursor::HeadUpper() const { return PeekUpper(0); }

Status TokenCursor::Consume(std::string* out) {
  if (AtEnd()) {
    return Status::Syntax("unexpected end of input", ParseErrorKind::kSyntax);
  }
  if (out != nullptr) {
    *out = tokens_[pos_];
  }
  ++pos_;
  return Status::OK();
}

Status TokenCursor::ExpectKeyword(const std::string& keyword) {
  if (AtEnd() || Peek() != keyword) {
    return Status::Syntax("expected keyword: " + keyword, ParseErrorKind::kSyntax);
  }
  ++pos_;
  return Status::OK();
}

Status TokenCursor::ExpectKeywordCI(const char* keyword) {
  if (AtEnd() || PeekUpper() != keyword) {
    return Status::Syntax(std::string("expected keyword: ") + keyword, ParseErrorKind::kSyntax);
  }
  ++pos_;
  return Status::OK();
}

std::string TokenCursor::RestSql() const {
  std::ostringstream sql;
  for (size_t i = pos_; i < tokens_.size(); ++i) {
    if (i > pos_) {
      sql << ' ';
    }
    sql << tokens_[i];
  }
  return sql.str();
}

}  // namespace heterodb::sql_parse
