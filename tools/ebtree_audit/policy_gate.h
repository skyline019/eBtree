#pragma once

#include "ebtree/engine/engine_attest.h"
#include "rar_types.h"

namespace ebtree {
namespace audit {

RarVerdict ApplyPolicyGate(const RarReport& report, std::string* reason_out);

RarVerdict EvaluateSnapshotPolicy(const AttestExportReportV2& snapshot,
                                  const RarPolicy& policy,
                                  std::string* reason_out);

}  // namespace audit
}  // namespace ebtree
