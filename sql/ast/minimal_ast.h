#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sql/catalog/row_codec.h"

namespace ebtree {
namespace sql {

enum class StmtKind {
  kOpen,
  kCreateTable,
  kInsert,
  kSelect,
  kUnknown,
};

enum class AttestationMode { kOff, kMonitor, kRequirePass, kAllowWarn };

struct OpenStmt {
  std::string path;
  std::string durability{"balanced"};
  uint64_t recovery_max_missing{0};
  AttestationMode attestation{AttestationMode::kMonitor};
};

struct CreateTableStmt {
  std::string table;
  std::string key_column{"key"};
  std::string value_column{"value"};
  std::vector<ColumnDef> columns;
};

struct InsertStmt {
  std::string table;
  std::vector<std::string> column_names;
  std::vector<std::string> values;
  std::string select_sql;
  std::string key;
  std::string value;
  std::string conflict_action;
};

struct SelectStmt {
  std::string table;
  std::string key;
  std::optional<uint64_t> max_pages;
};

struct SqlStatement {
  StmtKind kind{StmtKind::kUnknown};
  OpenStmt open{};
  CreateTableStmt create_table{};
  InsertStmt insert{};
  SelectStmt select{};
};

}  // namespace sql
}  // namespace ebtree
