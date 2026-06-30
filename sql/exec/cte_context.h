#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace ebtree {
namespace sql {

using RowMap = std::unordered_map<std::string, std::string>;

struct CteContext {
  std::unordered_map<std::string, std::vector<RowMap>> tables;

  bool Has(const std::string& name) const {
    return tables.find(name) != tables.end();
  }

  const std::vector<RowMap>* Get(const std::string& name) const {
    const auto it = tables.find(name);
    return it == tables.end() ? nullptr : &it->second;
  }
};

}  // namespace sql
}  // namespace ebtree
