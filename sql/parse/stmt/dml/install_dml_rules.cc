#include "sql/parse/native/stmt_handlers.h"

#include "sql/parse/core/first_match_registry.h"
#include "sql/parse/core/parse_context.h"

namespace ebtree {
namespace sql {
namespace parse {

void InstallDmlRules(FirstMatchRegistry* registry) {
  if (!registry) return;
  registry->Register(
      {"insert", 90,
       [](const ParseContext& c) { return c.cursor.PeekUpper() == "INSERT"; },
       ParseInsert});
  registry->Register(
      {"select", 90,
       [](const ParseContext& c) { return c.cursor.PeekUpper() == "SELECT"; },
       ParseSelectStmt});
  registry->Register(
      {"update", 90,
       [](const ParseContext& c) { return c.cursor.PeekUpper() == "UPDATE"; },
       ParseUpdate});
  registry->Register(
      {"delete", 90,
       [](const ParseContext& c) { return c.cursor.PeekUpper() == "DELETE"; },
       ParseDelete});
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
