#pragma once

#include <cstdint>
#include <optional>

#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace sql {

inline bool IsMicContractViolation(const Status& st) {
  return !st.ok() && st.message().rfind("MicContractViolation", 0) == 0;
}

Status CheckMicPagesBudget(Engine* engine, uint64_t pages_before,
                           std::optional<uint64_t> max_pages);

Status CheckMicRowBudget(size_t row_count, std::optional<uint64_t> max_pages);

}  // namespace sql
}  // namespace ebtree
