#pragma once

#include "sql/ast/minimal_ast.h"
#include "sql/session/open_options.h"
#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"

#include <memory>
#include <string>

namespace ebtree {
namespace sql {

enum class AttestationVerdict { kPass, kWarn, kRefuseStart };

struct AttestationReport {
  AttestationVerdict verdict{AttestationVerdict::kPass};
  std::string verdict_reason;
  bool any_badwal{false};
};

bool AttestationAllowsOpen(AttestationMode mode, AttestationVerdict verdict);

Status RunOpenAttestation(const OpenOptions& opts, AttestationReport* report,
                          std::unique_ptr<Engine>* engine_out);

}  // namespace sql
}  // namespace ebtree
