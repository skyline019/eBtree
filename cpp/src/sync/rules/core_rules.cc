#include "ebtree/engine/write_request.h"

#include "ebtree/concept/group_commit/group_committer.h"
#include "ebtree/concept/wal/wal.h"
#include "ebtree/engine/shard_engine.h"
#include "ebtree/sync/sync_executor.h"

namespace ebtree {

void RegisterCoreSyncRules(SyncExecutor* exec) {
  exec->Register(SyncEventType::kWrite, [](SyncContext* ctx) -> Status {
    ShardEngine* e = ctx->shard;
    WriteRequest* req = CurrentWriteRequest();
    if (!req || req->key.empty()) {
      return Status::InvalidArgument("empty write key");
    }
    uint64_t lsn = 0;
    const WalOp op = req->is_delete ? WalOp::kDelete : WalOp::kPut;
    const Status wal_st =
        e->wal()->Append(op, req->key, req->value, &lsn);
    if (!wal_st.ok()) return wal_st;
    e->mutable_stats()->wal_append_total++;

    if (req->is_delete) {
      e->memtable()->DeleteKey(req->key, lsn, req->txn_id, req->txn_id == 0);
    } else {
      const Status mt =
          e->memtable()->Put(req->key, req->value, lsn, req->txn_id,
                             req->txn_id == 0);
      if (!mt.ok()) return mt;
    }

    const DurabilityClass durability = e->durability();
    if (durability == DurabilityClass::kSync || e->options().sync_on_commit) {
      const Status fs = e->wal()->Fsync();
      if (!fs.ok()) return fs;
      e->mutable_stats()->stable_lsn = lsn;
    } else if (durability == DurabilityClass::kGroup) {
      GroupCommitter::RecordPut(lsn, e->group_commit_state());
      if (GroupCommitter::ShouldAutoCommit(*e->group_commit_state(),
                                           e->options().group_commit_batch_size)) {
        SyncContext gc_ctx;
        gc_ctx.shard = e;
        const Status gc =
            e->sync()->Dispatch(SyncEventType::kGroupCommit, &gc_ctx);
        if (!gc.ok()) return gc;
      }
    }
    return Status::Ok();
  });
}

void RegisterReadSyncRules(SyncExecutor* exec) {
  exec->Register(SyncEventType::kRead, [](SyncContext* ctx) -> Status {
    (void)ctx;
    return Status::Ok();
  });
}

}  // namespace ebtree
