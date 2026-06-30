#pragma once

#include "ebtree/engine/engine.h"
#include "sql/catalog/catalog.h"

namespace ebtree {
namespace sql {

inline Status SyncIndexEntries(Engine* engine, Catalog* catalog,
                               const TableSchema& table,
                               const std::string& pk,
                               const std::unordered_map<std::string, std::string>& fields,
                               bool remove) {
  if (!engine || !catalog) return Status::InvalidArgument("null engine/catalog");
  for (const auto& idx : catalog->IndexesForTable(table.name)) {
    if (idx.columns.empty()) continue;
    const auto& col = idx.columns[0];
    const auto it = fields.find(col);
    if (it == fields.end()) continue;
    const std::string ikey =
        catalog->EncodeIndexKey(table.id, idx.id, it->second, pk);
    if (remove) {
      const Status st = engine->Delete(ikey);
      if (!st.ok() && st.code() != StatusCode::kNotFound) return st;
    } else {
      const Status st = engine->Put(ikey, pk);
      if (!st.ok()) return st;
    }
  }
  return Status::Ok();
}

}  // namespace sql
}  // namespace ebtree
