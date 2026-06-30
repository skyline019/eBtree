#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ebtree {
namespace sql {
namespace parse {

std::string Trim(std::string s);
std::string Upper(std::string s);
bool StartsWithCI(const std::string& s, const char* prefix);
std::string UnquoteToken(const std::string& tok);
bool IsQuotedLiteral(const std::string& tok);
std::optional<uint64_t> ExtractMaxPagesHint(const std::string& sql);

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
