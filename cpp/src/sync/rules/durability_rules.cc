#include "ebtree/concept/group_commit/group_committer.h"
#include "ebtree/engine/shard_engine.h"
#include "ebtree/sync/sync_executor.h"

namespace ebtree {

void RegisterDurabilitySyncRules(SyncExecutor* exec) {
  exec->Register(SyncEventType::kGroupCommit, [](SyncContext* ctx) -> Status {
    ShardEngine* e = ctx->shard;
    return GroupCommitter::Commit(e->wal(), e->mutable_stats(),
                                    e->group_commit_state());
  });

  exec->Register(SyncEventType::kFlush, [](SyncContext* ctx) -> Status {
    return ctx->shard->FlushInternal();
  });

  exec->Register(SyncEventType::kSuperBlockCommit,
                 [](SyncContext* ctx) -> Status {
                   return ctx->shard->CommitSuperBlockInternal();
                 });
}

}  // namespace ebtree
