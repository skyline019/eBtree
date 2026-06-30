#include "token_cursor.h"

#include <cctype>

namespace ebtree {
namespace sql {
namespace parse {

namespace {

std::string UpperOf(const std::string& s) {
  std::string u = s;
  for (char& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return u;
}

}  // namespace

TokenCursor::TokenCursor(std::vector<std::string> tokens)
    : tokens_(std::move(tokens)) {}

void TokenCursor::Reset(std::vector<std::string> tokens) {
  tokens_ = std::move(tokens);
  pos_ = 0;
}

const std::string& TokenCursor::Peek(size_t offset) const {
  static const std::string kEmpty;
  if (pos_ + offset >= tokens_.size()) return kEmpty;
  return tokens_[pos_ + offset];
}

std::string TokenCursor::PeekUpper(size_t offset) const {
  return UpperOf(Peek(offset));
}

Status TokenCursor::Consume(std::string* out) {
  if (AtEnd()) return Status::InvalidArgument("unexpected end of sql");
  if (out) *out = tokens_[pos_++];
  else ++pos_;
  return Status::Ok();
}

Status TokenCursor::ExpectKeywordCI(const char* keyword) {
  if (PeekUpper() != keyword) {
    return Status::InvalidArgument(std::string("expected ") + keyword);
  }
  ++pos_;
  return Status::Ok();
}

std::string TokenCursor::RestSql() const {
  std::string out;
  for (size_t i = pos_; i < tokens_.size(); ++i) {
    if (i > pos_) out.push_back(' ');
    out += tokens_[i];
  }
  return out;
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
