#include "open_options.h"

#include <filesystem>

namespace ebtree {
namespace sql {

std::string OpenOptions::DefaultOpLogPath() const {
  if (!op_log_path.empty()) return op_log_path;
  return (std::filesystem::path(path) / "ebtree.op_log.jsonl").string();
}

std::string OpenOptions::DefaultCatalogPath() const {
  if (!catalog_path.empty()) return catalog_path;
  return (std::filesystem::path(path) / "ebtree.catalog.json").string();
}

EngineOptions OpenOptions::ToEngineOptions() const {
  EngineOptions opts = EngineOptions::ProductionDefaults(path);
  opts.path = path;
  opts.durability = durability;
  if (durability == DurabilityClass::kSync) {
    opts = EngineOptions::EnterpriseDefaults(path);
    opts.path = path;
    opts.durability = durability;
  } else if (durability == DurabilityClass::kGroup) {
    opts = EngineOptions::BenchmarkGroupDefaults(path);
    opts.path = path;
    opts.durability = durability;
  }
  return opts;
}

}  // namespace sql
}  // namespace ebtree
