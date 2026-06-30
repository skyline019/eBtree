#include "sql_parse/adapters/parse_catalog_bridge.h"

#include "concept/catalog/qualified_name.h"

namespace heterodb::sql_parse {

namespace {

class DefaultParseCatalogBridge final : public ParseCatalogBridge {
 public:
  Status ResolveQualifiedTable(const std::string& token,
                               const std::string& current_database,
                               std::string* database,
                               std::string* table) const override {
    if (database == nullptr || table == nullptr) {
      return Status::InvalidArgument("null output");
    }
    QualifiedTableName qn;
    Status s = SplitQualifiedTable(token, current_database, &qn);
    if (!s.ok()) {
      return s;
    }
    *database = qn.database;
    *table = qn.table;
    return Status::OK();
  }
};

}  // namespace

const ParseCatalogBridge& DefaultCatalogBridge() {
  static const DefaultParseCatalogBridge instance;
  return instance;
}

Status ResolveQualifiedTableFromToken(const std::string& token,
                                      const std::string& current_database,
                                      std::string* database, std::string* table) {
  return DefaultCatalogBridge().ResolveQualifiedTable(token, current_database,
                                                      database, table);
}

}  // namespace heterodb::sql_parse
