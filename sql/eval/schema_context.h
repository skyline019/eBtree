#pragma once

#include <string>
#include <unordered_map>

#include "sql/catalog/catalog.h"
#include "sql/eval/type_affinity.h"

namespace ebtree {
namespace sql {

struct SchemaContext {
  const TableSchema* table{nullptr};

  TypeAffinity ColumnAffinity(const std::string& col) const {
    if (!table) return TypeAffinity::kText;
    for (const auto& c : table->columns) {
      if (c.name == col) {
        const std::string u = c.type;
        if (u == "INTEGER") return TypeAffinity::kInteger;
        if (u == "REAL" || u == "FLOAT") return TypeAffinity::kReal;
        return TypeAffinity::kText;
      }
    }
    return TypeAffinity::kText;
  }
};

}  // namespace sql
}  // namespace ebtree
