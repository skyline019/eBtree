#include "mic_guard.h"

namespace ebtree {
namespace sql {

Status CheckMicPagesBudget(Engine* engine, uint64_t pages_before,
                           std::optional<uint64_t> max_pages) {
  if (!max_pages.has_value() || !engine) return Status::Ok();
  const uint64_t delta =
      engine->btree()->pages_touched() - pages_before;
  if (delta > *max_pages) {
    return Status::InvalidArgument(
        "MicContractViolation: pages_touched delta " +
        std::to_string(delta) + " exceeds max_pages " +
        std::to_string(*max_pages));
  }
  return Status::Ok();
}

Status CheckMicRowBudget(size_t row_count, std::optional<uint64_t> max_pages) {
  if (!max_pages.has_value()) return Status::Ok();
  if (row_count > *max_pages) {
    return Status::InvalidArgument(
        "MicContractViolation: row count " + std::to_string(row_count) +
        " exceeds max_pages " + std::to_string(*max_pages));
  }
  return Status::Ok();
}

}  // namespace sql
}  // namespace ebtree
