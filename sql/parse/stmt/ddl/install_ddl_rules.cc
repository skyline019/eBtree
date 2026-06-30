#include "sql/parse/native/stmt_handlers.h"

#include "sql/parse/core/first_match_registry.h"
#include "sql/parse/core/parse_context.h"

namespace ebtree {
namespace sql {
namespace parse {

void InstallDdlRules(FirstMatchRegistry* registry) {
  if (!registry) return;
  registry->Register(
      {"create", 90,
       [](const ParseContext& c) { return c.cursor.PeekUpper() == "CREATE"; },
       [](ParseContext* c) {
         if (c->cursor.PeekUpper(1) == "TABLE") return ParseCreateTable(c);
         if (c->cursor.PeekUpper(1) == "INDEX" ||
             (c->cursor.PeekUpper(1) == "UNIQUE" &&
              c->cursor.PeekUpper(2) == "INDEX")) {
           return ParseCreateIndex(c);
         }
         if (c->cursor.PeekUpper(1) == "VIEW" ||
             ((c->cursor.PeekUpper(1) == "TEMP" ||
               c->cursor.PeekUpper(1) == "TEMPORARY") &&
              c->cursor.PeekUpper(2) == "VIEW")) {
           return ParseCreateViewQuery(c->raw_sql, c->out);
         }
         if (c->cursor.PeekUpper(1) == "TRIGGER") {
           return ParseCreateTriggerQuery(c->raw_sql, c->out);
         }
         return Status::InvalidArgument("unsupported CREATE");
       }});
  registry->Register(
      {"drop", 80,
       [](const ParseContext& c) { return c.cursor.PeekUpper() == "DROP"; },
       [](ParseContext* c) {
         if (c->cursor.PeekUpper(1) == "TABLE") return ParseDropTable(c);
         if (c->cursor.PeekUpper(1) == "INDEX") return ParseDropIndex(c);
         if (c->cursor.PeekUpper(1) == "VIEW") {
           return ParseDropViewQuery(c->raw_sql, c->out);
         }
         if (c->cursor.PeekUpper(1) == "TRIGGER") {
           return ParseDropTriggerQuery(c->raw_sql, c->out);
         }
         return Status::InvalidArgument("unsupported DROP");
       }});
  registry->Register(
      {"alter", 80,
       [](const ParseContext& c) { return c.cursor.PeekUpper() == "ALTER"; },
       ParseAlterTable});
  registry->Register(
      {"reindex", 80,
       [](const ParseContext& c) { return c.cursor.PeekUpper() == "REINDEX"; },
       [](ParseContext* c) {
         return ParseReindexQuery(c->raw_sql, c->out);
       }});
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
