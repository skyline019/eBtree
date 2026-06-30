#include "constraint_check.h"

namespace ebtree {
namespace sql {

Status ValidateRowConstraints(
    const TableSchema& table,
    const std::unordered_map<std::string, std::string>& fields) {
  for (const auto& col : table.columns) {
    const auto it = fields.find(col.name);
    const std::string val = it != fields.end() ? it->second : "";
    if (col.not_null && val.empty()) {
      return Status::InvalidArgument("NOT NULL constraint failed: " + col.name);
    }
  }
  return Status::Ok();
}

}  // namespace sql
}  // namespace ebtree
