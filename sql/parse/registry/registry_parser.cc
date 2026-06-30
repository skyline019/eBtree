#include "registry_parser.h"

#include "sql/parse/native/native_parser.h"

namespace ebtree {
namespace sql {
namespace parse {

Status RegistryParser::Parse(const std::string& sql, QueryStatement* out) const {
  NativeParser native;
  return native.Parse(sql, out);
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
