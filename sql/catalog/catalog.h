#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sql/ast/expr_ast.h"
#include "sql/catalog/row_codec.h"
#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {

struct TableSchema {
  uint32_t id{0};
  std::string name;
  std::string key_column{"key"};
  std::string value_column{"value"};
  std::vector<ColumnDef> columns;
  uint32_t schema_version{1};
  bool compress_values{true};
  bool implicit_rowid{false};
  uint64_t next_rowid{1};
};

struct IndexDef {
  uint32_t id{0};
  std::string name;
  std::string table;
  std::vector<std::string> columns;
  bool unique{false};
};

struct ViewDef {
  std::string name;
  std::string table;
  std::string key_column;
  std::string value_column;
  std::shared_ptr<ExprNode> where_filter;
};

struct TriggerDef {
  std::string name;
  std::string table;
  std::string event{"INSERT"};
  std::string body_sql;
};

class Catalog {
 public:
  Status CreateTable(const std::string& name, const std::string& key_col,
                     const std::string& value_col, uint32_t* out_id);
  Status CreateTable(const std::string& name,
                     const std::vector<ColumnDef>& columns,
                     uint32_t* out_id);
  Status RestoreTable(uint32_t id, const std::string& name,
                      const std::string& key_col,
                      const std::string& value_col);
  Status RestoreTableV2(uint32_t id, const std::string& name,
                        const std::vector<ColumnDef>& columns);
  Status DropTable(const std::string& name);
  Status AddColumn(const std::string& table, const ColumnDef& col);
  Status DropColumn(const std::string& table, const std::string& col);
  Status CreateIndex(const std::string& name, const std::string& table,
                     const std::vector<std::string>& columns, bool unique,
                     uint32_t* out_id = nullptr);
  Status RestoreIndex(uint32_t id, const std::string& name,
                      const std::string& table,
                      const std::vector<std::string>& columns, bool unique);
  Status DropIndex(const std::string& name);
  Status CreateView(const std::string& name, const std::string& table,
                    const std::string& key_col, const std::string& value_col,
                    std::shared_ptr<ExprNode> where_filter = nullptr);
  Status DropView(const std::string& name);
  std::optional<ViewDef> FindView(const std::string& name) const;
  Status CreateTrigger(const TriggerDef& trigger);
  Status DropTrigger(const std::string& name);
  std::vector<TriggerDef> TriggersForTable(const std::string& table,
                                           const std::string& event) const;
  std::optional<IndexDef> FindIndex(const std::string& name) const;
  std::vector<IndexDef> IndexesForTable(const std::string& table) const;
  std::optional<TableSchema> FindTable(const std::string& name) const;
  Status AllocateRowKey(const std::string& table_name, std::string* out);
  std::string EncodeRowKey(uint32_t table_id, const std::string& key) const;
  std::string EncodeIndexKey(uint32_t table_id, uint32_t index_id,
                             const std::string& col_value,
                             const std::string& pk) const;
  bool DecodeRowKey(const std::string& encoded, uint32_t* table_id,
                    std::string* key) const;
  bool DecodeIndexKeyPrefix(const std::string& encoded, uint32_t* table_id,
                            uint32_t* index_id, std::string* col_value,
                            std::string* pk) const;
  static bool IsIndexEncodedKey(const std::string& encoded);
  const std::vector<TableSchema>& Tables() const { return table_list_; }
  const std::vector<IndexDef>& Indexes() const { return index_list_; }
  const std::vector<ViewDef>& Views() const { return view_list_; }
  void Clear();

 private:
  uint32_t next_id_{1};
  uint32_t next_index_id_{1};
  std::unordered_map<std::string, TableSchema> tables_;
  std::vector<TableSchema> table_list_;
  std::unordered_map<std::string, IndexDef> indexes_;
  std::vector<IndexDef> index_list_;
  std::unordered_map<std::string, ViewDef> views_;
  std::vector<ViewDef> view_list_;
  std::unordered_map<std::string, TriggerDef> triggers_;
  std::vector<TriggerDef> trigger_list_;
};

}  // namespace sql
}  // namespace ebtree
