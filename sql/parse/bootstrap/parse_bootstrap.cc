#include "parse_bootstrap.h"

#include "sql/parse/registry/registry_parser.h"

namespace ebtree {
namespace sql {
namespace parse {

ParseBootstrap& ParseBootstrap::Global() {
  static ParseBootstrap inst;
  return inst;
}

Status ParseBootstrap::Parse(const std::string& sql, QueryStatement* out) const {
  RegistryParser parser;
  return parser.Parse(sql, out);
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
