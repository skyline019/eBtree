#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {

struct ColumnDef {
  std::string name;
  std::string type{"TEXT"};
  bool not_null{false};
  bool primary_key{false};
  std::string check_expr;
};

Status EncodeRowBinary(const std::vector<ColumnDef>& cols,
                       const std::unordered_map<std::string, std::string>& values,
                       std::string* out);

Status DecodeRow(const std::string& stored,
                 std::unordered_map<std::string, std::string>* out);

Status EncodeRowJson(const std::vector<ColumnDef>& cols,
                     const std::unordered_map<std::string, std::string>& values,
                     std::string* out);

Status DecodeRowJson(const std::string& json,
                     std::unordered_map<std::string, std::string>* out);

Status UpdateRowJson(const std::string& json, const std::string& col,
                     const std::string& value, std::string* out);

Status UpdateRowFields(const std::string& stored, const std::string& col,
                       const std::string& value, std::string* out);

Status EncodeRow(const std::vector<ColumnDef>& cols, uint32_t schema_version,
                 const std::unordered_map<std::string, std::string>& values,
                 std::string* out);

Status DecodeRowFields(const std::string& stored,
                       std::unordered_map<std::string, std::string>* out);

bool IsBinaryRow(const std::string& stored);

}  // namespace sql
}  // namespace ebtree
