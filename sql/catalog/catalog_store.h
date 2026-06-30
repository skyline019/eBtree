#pragma once

#include <string>

#include "sql/catalog/catalog.h"
#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {

class CatalogStore {
 public:
  explicit CatalogStore(std::string path);

  Status Load(Catalog* catalog);
  Status Save(const Catalog& catalog) const;

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

std::string DefaultCatalogPath(const std::string& engine_path);

}  // namespace sql
}  // namespace ebtree
