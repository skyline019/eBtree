#pragma once

#include <string>

#include "common/status.h"

namespace heterodb::sql_parse {

/** Injectable catalog resolution (tests may substitute). */
class ParseCatalogBridge {
 public:
  virtual ~ParseCatalogBridge() = default;
  virtual Status ResolveQualifiedTable(const std::string& token,
                                       const std::string& current_database,
                                       std::string* database,
                                       std::string* table) const = 0;
};

const ParseCatalogBridge& DefaultCatalogBridge();

Status ResolveQualifiedTableFromToken(const std::string& token,
                                      const std::string& current_database,
                                      std::string* database, std::string* table);

}  // namespace heterodb::sql_parse
