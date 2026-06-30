#pragma once

#include <cstdint>
#include <string>

#include "sql/ast/minimal_ast.h"
#include "ebtree/common/config.h"

namespace ebtree {
namespace sql {

struct OpenOptions {
  std::string path;
  DurabilityClass durability{DurabilityClass::kBalanced};
  uint64_t recovery_max_missing{0};
  AttestationMode attestation{AttestationMode::kOff};
  std::string op_log_path;
  std::string catalog_path;

  std::string DefaultOpLogPath() const;
  std::string DefaultCatalogPath() const;
  EngineOptions ToEngineOptions() const;
};

}  // namespace sql
}  // namespace ebtree
