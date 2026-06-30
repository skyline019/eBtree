#pragma once

#include "rar_types.h"

#include <memory>

#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace audit {

struct BuildRarOptions {
  std::string engine_path;
  DurabilityClass durability_tier{DurabilityClass::kBalanced};
  uint32_t shard_count{0};
  std::vector<std::string> probe_keys;
  const ExpectSnapshot* expect{nullptr};
  RarPolicy policy{};
  bool include_recovery{true};
  bool skip_physical_if_engine_open{false};
  std::string catalog_path;
  std::string op_log_path;
  EngineOptions engine_options{};
};

Status BuildRar(const BuildRarOptions& opts, RarReport* out,
                std::unique_ptr<Engine>* engine_out = nullptr);

Status BuildPhysicalOnly(const std::string& engine_path, RarReport* out);

}  // namespace audit
}  // namespace ebtree
