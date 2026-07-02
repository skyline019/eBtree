#pragma once

#include <string>

#include "ebtree/engine/engine_attest.h"

namespace ebtree {
namespace audit {

std::string AttestExportV2ToJson(const AttestExportReportV2& report);

std::string BuildChainBodyJson(uint64_t sequence, uint64_t checkpoint_lsn,
                               const std::string& prev_rar_sha256,
                               const std::string& op_log_head_sha256,
                               int64_t generated_at_unix,
                               const AttestExportReportV2& kernel);

}  // namespace audit
}  // namespace ebtree
