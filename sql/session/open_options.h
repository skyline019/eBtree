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
  AttestationMode attestation{AttestationMode::kMonitor};
  std::string op_log_path;
  std::string catalog_path;
  std::string rar_chain_path;
  bool attestation_async{true};

  std::string DefaultOpLogPath() const;
  std::string DefaultCatalogPath() const;
  std::string DefaultRarChainPath() const;
  EngineOptions ToEngineOptions() const;
};

}  // namespace sql
}  // namespace ebtree
