#include "catalog.h"

#include <algorithm>

namespace ebtree {
namespace sql {

Status Catalog::CreateTable(const std::string& name, const std::string& key_col,
                            const std::string& value_col, uint32_t* out_id) {
  std::vector<ColumnDef> cols;
  cols.push_back({key_col, "TEXT"});
  cols.push_back({value_col, "TEXT"});
  return CreateTable(name, cols, out_id);
}

Status Catalog::CreateTable(const std::string& name,
                            const std::vector<ColumnDef>& columns,
                            uint32_t* out_id) {
  if (name.empty()) return Status::InvalidArgument("empty table name");
  if (tables_.count(name)) {
    return Status::InvalidArgument("table already exists: " + name);
  }
  if (columns.empty()) return Status::InvalidArgument("empty columns");
  TableSchema schema{};
  schema.id = next_id_++;
  schema.name = name;
  schema.columns = columns;
  bool has_pk = false;
  schema.key_column = columns[0].name;
  for (const auto& c : columns) {
    if (c.primary_key) {
      schema.key_column = c.name;
      has_pk = true;
      break;
    }
  }
  schema.implicit_rowid = !has_pk;
  schema.next_rowid = 1;
  schema.value_column = columns.size() > 1 ? columns[1].name : columns[0].name;
  schema.schema_version = 3;
  if (out_id) *out_id = schema.id;
  tables_[name] = schema;
  table_list_.push_back(schema);
  return Status::Ok();
}

Status Catalog::RestoreTable(uint32_t id, const std::string& name,
                             const std::string& key_col,
                             const std::string& value_col) {
  std::vector<ColumnDef> cols{{key_col, "TEXT"}, {value_col, "TEXT"}};
  return RestoreTableV2(id, name, cols);
}

Status Catalog::RestoreTableV2(uint32_t id, const std::string& name,
                               const std::vector<ColumnDef>& columns) {
  if (name.empty()) return Status::InvalidArgument("empty table name");
  if (tables_.count(name)) {
    return Status::InvalidArgument("duplicate table in catalog sidecar: " + name);
  }
  TableSchema schema{};
  schema.id = id;
  schema.name = name;
  schema.columns = columns;
  schema.key_column = columns.empty() ? "key" : columns[0].name;
  schema.value_column = columns.size() > 1 ? columns[1].name : schema.key_column;
  schema.schema_version = 2;
  next_id_ = std::max(next_id_, id + 1);
  tables_[name] = schema;
  table_list_.push_back(schema);
  return Status::Ok();
}

Status Catalog::AddColumn(const std::string& table, const ColumnDef& col) {
  const auto it = tables_.find(table);
  if (it == tables_.end()) return Status::InvalidArgument("unknown table");
  for (const auto& c : it->second.columns) {
    if (c.name == col.name) {
      return Status::InvalidArgument("duplicate column");
    }
  }
  it->second.columns.push_back(col);
  it->second.schema_version = 2;
  for (auto& t : table_list_) {
    if (t.id == it->second.id) {
      t = it->second;
      break;
    }
  }
  return Status::Ok();
}

Status Catalog::DropColumn(const std::string& table, const std::string& col) {
  const auto it = tables_.find(table);
  if (it == tables_.end()) return Status::InvalidArgument("unknown table");
  auto& cols = it->second.columns;
  cols.erase(std::remove_if(cols.begin(), cols.end(),
                            [&](const ColumnDef& c) { return c.name == col; }),
             cols.end());
  it->second.schema_version = 2;
  return Status::Ok();
}

void Catalog::Clear() {
  tables_.clear();
  table_list_.clear();
  indexes_.clear();
  index_list_.clear();
  views_.clear();
  view_list_.clear();
  triggers_.clear();
  trigger_list_.clear();
  next_id_ = 1;
  next_index_id_ = 1;
}

Status Catalog::DropTable(const std::string& name) {
  const auto it = tables_.find(name);
  if (it == tables_.end()) {
    return Status::InvalidArgument("unknown table: " + name);
  }
  const uint32_t id = it->second.id;
  tables_.erase(it);
  table_list_.erase(
      std::remove_if(table_list_.begin(), table_list_.end(),
                     [id](const TableSchema& s) { return s.id == id; }),
      table_list_.end());
  for (auto tri = triggers_.begin(); tri != triggers_.end();) {
    if (tri->second.table == name) {
      tri = triggers_.erase(tri);
    } else {
      ++tri;
    }
  }
  trigger_list_.erase(
      std::remove_if(trigger_list_.begin(), trigger_list_.end(),
                     [&](const TriggerDef& t) { return t.table == name; }),
      trigger_list_.end());
  return Status::Ok();
}

Status Catalog::AllocateRowKey(const std::string& table_name, std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  const auto it = tables_.find(table_name);
  if (it == tables_.end()) {
    return Status::InvalidArgument("unknown table: " + table_name);
  }
  if (!it->second.implicit_rowid) {
    return Status::InvalidArgument("table has no implicit rowid: " + table_name);
  }
  *out = std::to_string(it->second.next_rowid++);
  for (auto& t : table_list_) {
    if (t.id == it->second.id) {
      t.next_rowid = it->second.next_rowid;
      break;
    }
  }
  return Status::Ok();
}

std::optional<TableSchema> Catalog::FindTable(const std::string& name) const {
  const auto it = tables_.find(name);
  if (it == tables_.end()) return std::nullopt;
  return it->second;
}

bool Catalog::IsIndexEncodedKey(const std::string& encoded) {
  const auto pos = encoded.find(':');
  if (pos == std::string::npos || pos + 1 >= encoded.size()) return false;
  return encoded[pos + 1] == 'i';
}

std::string Catalog::EncodeRowKey(uint32_t table_id,
                                  const std::string& key) const {
  return std::to_string(table_id) + ":" + key;
}

bool Catalog::DecodeRowKey(const std::string& encoded, uint32_t* table_id,
                           std::string* key) const {
  const auto pos = encoded.find(':');
  if (pos == std::string::npos || pos == 0) return false;
  const std::string prefix = encoded.substr(0, pos);
  if (prefix.size() >= 2 && prefix[1] == 'i') return false;
  if (table_id) *table_id = static_cast<uint32_t>(std::stoul(prefix));
  if (key) *key = encoded.substr(pos + 1);
  return true;
}

std::string Catalog::EncodeIndexKey(uint32_t table_id, uint32_t index_id,
                                    const std::string& col_value,
                                    const std::string& pk) const {
  return std::to_string(table_id) + ":i" + std::to_string(index_id) + ":" +
         col_value + ":" + pk;
}

bool Catalog::DecodeIndexKeyPrefix(const std::string& encoded, uint32_t* table_id,
                                   uint32_t* index_id, std::string* col_value,
                                   std::string* pk) const {
  const auto c1 = encoded.find(':');
  if (c1 == std::string::npos) return false;
  const auto c2 = encoded.find(':', c1 + 1);
  if (c2 == std::string::npos) return false;
  const std::string idx_part = encoded.substr(c1 + 1, c2 - c1 - 1);
  if (idx_part.empty() || idx_part[0] != 'i') return false;
  const auto c3 = encoded.find(':', c2 + 1);
  if (c3 == std::string::npos) return false;
  if (table_id) *table_id = static_cast<uint32_t>(std::stoul(encoded.substr(0, c1)));
  if (index_id) *index_id = static_cast<uint32_t>(std::stoul(idx_part.substr(1)));
  if (col_value) *col_value = encoded.substr(c2 + 1, c3 - c2 - 1);
  if (pk) *pk = encoded.substr(c3 + 1);
  return true;
}

Status Catalog::RestoreIndex(uint32_t id, const std::string& name,
                             const std::string& table,
                             const std::vector<std::string>& columns,
                             bool unique) {
  if (name.empty() || table.empty() || columns.empty()) {
    return Status::InvalidArgument("invalid index restore");
  }
  if (indexes_.count(name)) {
    return Status::InvalidArgument("duplicate index in catalog sidecar: " + name);
  }
  IndexDef idx{};
  idx.id = id;
  idx.name = name;
  idx.table = table;
  idx.columns = columns;
  idx.unique = unique;
  next_index_id_ = std::max(next_index_id_, id + 1);
  indexes_[name] = idx;
  index_list_.push_back(idx);
  return Status::Ok();
}

Status Catalog::CreateIndex(const std::string& name, const std::string& table,
                            const std::vector<std::string>& columns, bool unique,
                            uint32_t* out_id) {
  if (name.empty() || table.empty() || columns.empty()) {
    return Status::InvalidArgument("invalid index");
  }
  if (!FindTable(table)) return Status::InvalidArgument("unknown table");
  if (indexes_.count(name)) return Status::InvalidArgument("index exists");
  IndexDef idx{};
  idx.id = next_index_id_++;
  idx.name = name;
  idx.table = table;
  idx.columns = columns;
  idx.unique = unique;
  if (out_id) *out_id = idx.id;
  indexes_[name] = idx;
  index_list_.push_back(idx);
  return Status::Ok();
}

Status Catalog::DropIndex(const std::string& name) {
  const auto it = indexes_.find(name);
  if (it == indexes_.end()) return Status::InvalidArgument("unknown index");
  const uint32_t id = it->second.id;
  indexes_.erase(it);
  index_list_.erase(
      std::remove_if(index_list_.begin(), index_list_.end(),
                     [id](const IndexDef& d) { return d.id == id; }),
      index_list_.end());
  return Status::Ok();
}

std::optional<IndexDef> Catalog::FindIndex(const std::string& name) const {
  const auto it = indexes_.find(name);
  if (it == indexes_.end()) return std::nullopt;
  return it->second;
}

std::vector<IndexDef> Catalog::IndexesForTable(const std::string& table) const {
  std::vector<IndexDef> out;
  for (const auto& idx : index_list_) {
    if (idx.table == table) out.push_back(idx);
  }
  return out;
}

Status Catalog::CreateView(const std::string& name, const std::string& table,
                           const std::string& key_col, const std::string& value_col,
                           std::shared_ptr<ExprNode> where_filter) {
  if (views_.count(name)) {
    return Status::InvalidArgument("view already exists: " + name);
  }
  ViewDef v{name, table, key_col, value_col, std::move(where_filter)};
  views_[name] = v;
  view_list_.push_back(v);
  return Status::Ok();
}

Status Catalog::DropView(const std::string& name) {
  const auto it = views_.find(name);
  if (it == views_.end()) return Status::InvalidArgument("unknown view");
  views_.erase(it);
  view_list_.erase(
      std::remove_if(view_list_.begin(), view_list_.end(),
                     [&](const ViewDef& v) { return v.name == name; }),
      view_list_.end());
  return Status::Ok();
}

std::optional<ViewDef> Catalog::FindView(const std::string& name) const {
  const auto it = views_.find(name);
  if (it == views_.end()) return std::nullopt;
  return it->second;
}

Status Catalog::CreateTrigger(const TriggerDef& trigger) {
  if (triggers_.count(trigger.name)) {
    return Status::InvalidArgument("trigger exists: " + trigger.name);
  }
  triggers_[trigger.name] = trigger;
  trigger_list_.push_back(trigger);
  return Status::Ok();
}

Status Catalog::DropTrigger(const std::string& name) {
  const auto it = triggers_.find(name);
  if (it == triggers_.end()) return Status::InvalidArgument("unknown trigger");
  triggers_.erase(it);
  trigger_list_.erase(
      std::remove_if(trigger_list_.begin(), trigger_list_.end(),
                     [&](const TriggerDef& t) { return t.name == name; }),
      trigger_list_.end());
  return Status::Ok();
}

std::vector<TriggerDef> Catalog::TriggersForTable(const std::string& table,
                                                  const std::string& event) const {
  std::vector<TriggerDef> out;
  for (const auto& t : trigger_list_) {
    if (t.table == table && t.event == event) out.push_back(t);
  }
  return out;
}

}  // namespace sql
}  // namespace ebtree
