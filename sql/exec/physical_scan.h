#pragma once

#include <string>
#include <utility>
#include <vector>

#include "sql/catalog/catalog.h"
#include "sql/session/transaction_state.h"
#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace sql {

class PhysicalScan {
 public:
  PhysicalScan(ebtree::Engine* engine, Catalog* catalog,
               TransactionState* txn = nullptr)
      : engine_(engine), catalog_(catalog), txn_(txn) {}

  Status ScanTable(const TableSchema& table,
                   std::vector<std::pair<std::string, std::string>>* rows) const;

  Status ScanIndexEq(const TableSchema& table, const IndexDef& idx,
                     const std::string& col_value,
                     std::vector<std::pair<std::string, std::string>>* rows) const;

  Status ScanIndexRange(const TableSchema& table, const IndexDef& idx,
                        const std::string& range_lo, const std::string& range_hi,
                        std::vector<std::pair<std::string, std::string>>* rows) const;

 private:
  Status EngineGet(const std::string& key, std::string* value) const;
  Status EngineScan(const TypedPlan& plan,
                    std::vector<std::pair<std::string, std::string>>* rows) const;

  Status FetchRowsFromIndexKeys(
      const TableSchema& table,
      const std::vector<std::pair<std::string, std::string>>& index_rows,
      std::vector<std::pair<std::string, std::string>>* rows) const;

  ebtree::Engine* engine_{nullptr};
  Catalog* catalog_{nullptr};
  TransactionState* txn_{nullptr};
};

}  // namespace sql
}  // namespace ebtree
