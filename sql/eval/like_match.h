#pragma once

#include <string>

namespace ebtree {
namespace sql {

bool SqlLikeMatch(const std::string& text, const std::string& pattern,
                  char escape = '\0');
bool SqlGlobMatch(const std::string& text, const std::string& pattern);

}  // namespace sql
}  // namespace ebtree
