#pragma once

#include <string>

#include "concept/storage/storage_engine.h"

namespace heterodb::sql_parse {

EngineKind ParseStorageEngineKindName(const std::string& name, bool* ok);

}  // namespace heterodb::sql_parse
