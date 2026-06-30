#include "ebtree/sync/sync_executor.h"

namespace ebtree {

void SyncExecutor::Register(SyncEventType type, SyncHandler handler) {
  rules_.emplace_back(type, std::move(handler));
}

Status SyncExecutor::Dispatch(SyncEventType type, SyncContext* ctx) {
  for (const auto& rule : rules_) {
    if (rule.first != type) continue;
    const Status st = rule.second(ctx);
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

}  // namespace ebtree
