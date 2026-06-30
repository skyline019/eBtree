#pragma once

#include <string>
#include <utility>
#include <vector>

#include "sql/catalog/catalog.h"
#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace sql {

class PhysicalScan {
 public:
  PhysicalScan(ebtree::Engine* engine, Catalog* catalog)
      : engine_(engine), catalog_(catalog) {}

  Status ScanTable(const TableSchema& table,
                   std::vector<std::pair<std::string, std::string>>* rows) const;

 private:
  ebtree::Engine* engine_{nullptr};
  Catalog* catalog_{nullptr};
};

}  // namespace sql
}  // namespace ebtree
