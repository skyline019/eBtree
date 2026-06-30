#include "sql_parse/adapters/parse_schema_bridge.h"

namespace heterodb::sql_parse {

EngineKind ParseStorageEngineKindName(const std::string& name, bool* ok) {
  return ParseEngineKindName(name, ok);
}

}  // namespace heterodb::sql_parse
