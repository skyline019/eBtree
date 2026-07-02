#include "ebtree/engine/engine.h"

namespace ebtree {

Status Engine::AppendTxnBegin(uint32_t txn_id, const SnapshotToken& token) {
  if (!opened_) return Status::Internal("engine not open");
  Status last = Status::Ok();
  for (uint32_t i = 0; i < shard_count(); ++i) {
    const Status es = EnsureShard(i);
    if (!es.ok()) return es;
    const Status st =
        shards_[i]->AppendTxnBegin(txn_id, token.ForShard(i));
    if (!st.ok()) last = st;
  }
  return last;
}

Status Engine::AppendTxnCommit(uint32_t txn_id) {
  if (!opened_) return Status::Internal("engine not open");
  Status last = Status::Ok();
  for (uint32_t i = 0; i < shard_count(); ++i) {
    if (!shards_[i]) continue;
    const Status st = shards_[i]->AppendTxnCommit(txn_id);
    if (!st.ok()) last = st;
  }
  return last;
}

Status Engine::AppendTxnAbort(uint32_t txn_id) {
  if (!opened_) return Status::Internal("engine not open");
  Status last = Status::Ok();
  for (uint32_t i = 0; i < shard_count(); ++i) {
    if (!shards_[i]) continue;
    const Status st = shards_[i]->AppendTxnAbort(txn_id);
    if (!st.ok()) last = st;
  }
  return last;
}

void Engine::RegisterRangeTicket(uint32_t txn_id, uint64_t snapshot_lsn,
                                   const std::string& key_lo,
                                   const std::string& key_hi) {
  RangeTicketRegistry::ForPath(opts_.path)
      .Register(txn_id, snapshot_lsn, key_lo, key_hi);
}

void Engine::UnregisterRangeTickets(uint32_t txn_id) {
  RangeTicketRegistry::ForPath(opts_.path).UnregisterTxn(txn_id);
}

Status Engine::ValidateRangeTicketCommit(
    uint32_t txn_id, uint64_t snapshot_lsn, const std::string& key_lo,
    const std::string& key_hi,
    const std::unordered_map<std::string, uint64_t>& write_set) const {
  return RangeTicketRegistry::ForPath(opts_.path)
      .ValidateCommit(txn_id, snapshot_lsn, key_lo, key_hi, write_set);
}

}  // namespace ebtree
