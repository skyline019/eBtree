#include "ebtree/engine/shard_engine.h"
#include "ebtree/sync/sync_executor.h"

namespace ebtree {

void RegisterRecoverySyncRules(SyncExecutor* exec) {
  exec->Register(SyncEventType::kRecovery, [](SyncContext* ctx) -> Status {
    ShardEngine* e = ctx->shard;
    SuperBlock sb{};
    const Status ls = e->superblock()->Load(&sb);
    if (!ls.ok() && ls.code() != StatusCode::kCorrupt) {
      return ls;
    }
    if (ls.code() == StatusCode::kCorrupt) {
      return Status::Corrupt("CorruptSuperBlock");
    }
    return e->RecoverFromSuperBlock(sb);
  });
}

}  // namespace ebtree
