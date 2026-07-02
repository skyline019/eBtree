#include "ebtree/concept/flusher/flusher.h"

#include <algorithm>

namespace ebtree {

Status Flusher::Flush(FlusherContext* ctx) {
  if (!ctx || !ctx->wal || !ctx->frozen || !ctx->datafile || !ctx->btree ||
      !ctx->committed || !ctx->stats) {
    return Status::InvalidArgument("invalid flusher context");
  }

  const Status fs = ctx->wal->Fsync();
  if (!fs.ok()) return fs;

  const auto snap = ctx->frozen->Snapshot();
  for (const auto& kv : snap) {
    uint64_t prev_lsn = 0;
    (void)ctx->btree->Get(kv.first, &prev_lsn);
    if (kv.second.deleted) {
      const Status ds =
          ctx->datafile->Append(kv.second.lsn, kv.first, "", true, ctx->generation);
      if (!ds.ok()) return ds;
      ctx->committed->erase(kv.first);
      (void)ctx->btree->DeleteKey(kv.first, kv.second.lsn);
      if (ctx->vcs) {
        const Status vs =
            ctx->vcs->Append(kv.first, kv.second.lsn, prev_lsn);
        if (!vs.ok()) return vs;
      }
    } else {
      const Status ds = ctx->datafile->Append(kv.second.lsn, kv.first,
                                              kv.second.value, false,
                                              ctx->generation);
      if (!ds.ok()) return ds;
      (*ctx->committed)[kv.first] = {kv.second.value, kv.second.lsn};
      (void)ctx->btree->Put(kv.first, kv.second.lsn);
      if (ctx->vcs) {
        const Status vs =
            ctx->vcs->Append(kv.first, kv.second.lsn, prev_lsn);
        if (!vs.ok()) return vs;
      }
    }
    ctx->stats->stable_lsn = std::max(ctx->stats->stable_lsn, kv.second.lsn);
  }
  ctx->datafile->FlushAppendStream();
  ctx->frozen->Clear();
  ctx->stats->flusher_flush_total++;
  return Status::Ok();
}

}  // namespace ebtree
