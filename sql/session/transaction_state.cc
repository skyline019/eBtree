#include "transaction_state.h"

#include "sql/catalog/catalog.h"

namespace ebtree {
namespace sql {

namespace {
std::atomic<uint32_t> g_next_txn_id{1};
}  // namespace

uint32_t TransactionState::AllocateTxnId() {
  return g_next_txn_id.fetch_add(1, std::memory_order_relaxed);
}

Status TransactionState::SampleReadVersion(Engine* engine,
                                           const std::string& encoded_key,
                                           uint64_t* lsn_out) {
  if (!engine || !lsn_out) return Status::InvalidArgument("null out");
  return engine->ResolveLsnAtSnapshot(encoded_key, snapshot_token_, txn_id_,
                                      lsn_out);
}

Status TransactionState::ReadKey(Engine* engine, const std::string& encoded_key,
                                 std::string* value) {
  if (!engine || !value) return Status::InvalidArgument("null out");
  if (!active_) return engine->Get(encoded_key, value);

  uint64_t visible_lsn = 0;
  const Status sample = SampleReadVersion(engine, encoded_key, &visible_lsn);
  if (!sample.ok() && sample.code() != StatusCode::kNotFound) return sample;
  if (sample.ok()) {
    read_set_[encoded_key] = visible_lsn;
  } else if (sample.code() == StatusCode::kNotFound) {
    uint64_t committed = 0;
    if (engine->ResolveCurrentCommittedLsn(encoded_key, &committed).ok()) {
      read_set_[encoded_key] = committed;
    } else {
      read_set_[encoded_key] = 0;
    }
  }

  return engine->GetAtSnapshot(encoded_key, snapshot_token_, txn_id_, value);
}

void TransactionState::RecordWriteIntent(Engine* engine,
                                         const std::string& encoded_key) {
  if (!active_ || !engine) return;
  const auto read_it = read_set_.find(encoded_key);
  if (read_it != read_set_.end()) {
    if (read_it->second == 0) {
      uint64_t committed = 0;
      if (engine->ResolveCurrentCommittedLsn(encoded_key, &committed).ok()) {
        write_set_[encoded_key] = committed;
        read_set_[encoded_key] = committed;
        return;
      }
    }
    write_set_[encoded_key] = read_it->second;
    return;
  }
  uint64_t visible_lsn = 0;
  const Status sample = SampleReadVersion(engine, encoded_key, &visible_lsn);
  if (sample.ok()) {
    read_set_[encoded_key] = visible_lsn;
    write_set_[encoded_key] = visible_lsn;
  } else if (sample.code() == StatusCode::kNotFound) {
    read_set_[encoded_key] = 0;
    write_set_[encoded_key] = 0;
  }
}

void TransactionState::RecordReadSample(Engine* engine,
                                        const std::string& encoded_key) {
  if (!active_ || !engine) return;
  if (Catalog::IsIndexEncodedKey(encoded_key)) return;
  uint64_t visible_lsn = 0;
  const Status sample = SampleReadVersion(engine, encoded_key, &visible_lsn);
  if (sample.ok()) {
    read_set_[encoded_key] = visible_lsn;
  } else if (sample.code() == StatusCode::kNotFound) {
    read_set_[encoded_key] = 0;
  }
}

void TransactionState::RegisterRangeScan(Engine* engine,
                                         const std::string& key_lo,
                                         const std::string& key_hi) {
  if (!active_) return;
  RangeTicket ticket{};
  ticket.snapshot_lsn = snapshot_token_.ForShard(0);
  ticket.key_lo = key_lo;
  ticket.key_hi = key_hi;
  range_tickets_.push_back(ticket);
  if (engine) {
    engine->RegisterRangeTicket(txn_id_, ticket.snapshot_lsn, key_lo, key_hi);
  }
}

Status TransactionState::Begin(Engine* engine) {
  if (active_) {
    return Status::InvalidArgument("transaction already active");
  }
  active_ = true;
  txn_id_ = AllocateTxnId();
  snapshot_token_ = engine ? engine->CaptureSnapshot() : SnapshotToken{};
  read_set_.clear();
  write_set_.clear();
  range_tickets_.clear();
  if (engine) {
    (void)engine->PinSnapshot(snapshot_token_);
    (void)engine->AppendTxnBegin(txn_id_, snapshot_token_);
  }
  journal_.clear();
  savepoints_.clear();
  savepoint_names_.clear();
  return Status::Ok();
}

Status TransactionState::ValidateCommitOCC(Engine* engine) const {
  if (!engine) return Status::InvalidArgument("null engine");
  const Status refresh = engine->RefreshExternalWalForOCC();
  if (!refresh.ok()) return refresh;

  for (const auto& kv : read_set_) {
    if (write_set_.count(kv.first) > 0) continue;
    uint64_t cur = 0;
    const Status rs = engine->ResolveCurrentCommittedLsn(kv.first, &cur);
    if (!rs.ok() && rs.code() != StatusCode::kNotFound) return rs;
    const uint64_t current = rs.ok() ? cur : 0;
    if (kv.second != current) {
      return Status::Conflict("snapshot conflict on key");
    }
  }

  for (const auto& kv : write_set_) {
    uint64_t cur = 0;
    const Status rs = engine->ResolveCurrentCommittedLsn(kv.first, &cur);
    if (!rs.ok() && rs.code() != StatusCode::kNotFound) return rs;
    const uint64_t current = rs.ok() ? cur : 0;
    if (kv.second != current) {
      return Status::Conflict("write ticket stale");
    }
  }
  const Status phantom = engine->ValidateRangeTicketCommit(
      txn_id_, snapshot_token_.ForShard(0), "", "", write_set_);
  if (!phantom.ok()) return phantom;
  return Status::Ok();
}

Status TransactionState::Commit(Engine* engine, DurabilityClass tier) {
  if (!active_) {
    return Status::InvalidArgument("no active transaction");
  }
  if (engine) {
    const Status occ = ValidateCommitOCC(engine);
    if (!occ.ok()) return occ;
    const Status wal = engine->AppendTxnCommit(txn_id_);
    if (!wal.ok()) return wal;
    engine->memtable()->PromoteTxn(txn_id_);
    engine->immutable_memtable()->PromoteTxn(txn_id_);
    engine->flushing_memtable()->PromoteTxn(txn_id_);
    engine->UnregisterRangeTickets(txn_id_);
    engine->ReleaseSnapshot(snapshot_token_);
  }
  active_ = false;
  txn_id_ = 0;
  snapshot_token_ = {};
  read_set_.clear();
  write_set_.clear();
  range_tickets_.clear();
  journal_.clear();
  savepoints_.clear();
  savepoint_names_.clear();
  if (tier == DurabilityClass::kGroup && engine) {
    return engine->GroupCommit();
  }
  return Status::Ok();
}

Status TransactionState::Rollback(Engine* engine) {
  if (!active_) {
    return Status::InvalidArgument("no active transaction");
  }
  if (engine) {
    (void)engine->AppendTxnAbort(txn_id_);
    engine->memtable()->ClearTxn(txn_id_);
    engine->immutable_memtable()->ClearTxn(txn_id_);
    engine->flushing_memtable()->ClearTxn(txn_id_);
    engine->UnregisterRangeTickets(txn_id_);
    for (auto it = journal_.rbegin(); it != journal_.rend(); ++it) {
      if (it->had_value) {
        (void)engine->Put(it->engine_key, it->old_value, 0);
      } else {
        (void)engine->Delete(it->engine_key, 0);
      }
    }
    engine->ReleaseSnapshot(snapshot_token_);
  }
  active_ = false;
  txn_id_ = 0;
  snapshot_token_ = {};
  read_set_.clear();
  write_set_.clear();
  range_tickets_.clear();
  journal_.clear();
  savepoints_.clear();
  savepoint_names_.clear();
  return Status::Ok();
}

Status TransactionState::Savepoint(const std::string& name) {
  if (!active_) {
    return Status::InvalidArgument("no active transaction");
  }
  savepoints_.push_back(journal_.size());
  savepoint_names_.push_back(name);
  return Status::Ok();
}

Status TransactionState::ReleaseSavepoint(const std::string& name) {
  if (!active_) {
    return Status::InvalidArgument("no active transaction");
  }
  for (size_t i = 0; i < savepoint_names_.size(); ++i) {
    if (savepoint_names_[i] == name) {
      savepoint_names_.erase(savepoint_names_.begin() +
                             static_cast<std::ptrdiff_t>(i));
      savepoints_.erase(savepoints_.begin() + static_cast<std::ptrdiff_t>(i));
      return Status::Ok();
    }
  }
  return Status::InvalidArgument("savepoint not found");
}

Status TransactionState::RollbackToSavepoint(Engine* engine,
                                             const std::string& name) {
  if (!active_) {
    return Status::InvalidArgument("no active transaction");
  }
  size_t idx = savepoint_names_.size();
  for (size_t i = 0; i < savepoint_names_.size(); ++i) {
    if (savepoint_names_[i] == name) {
      idx = i;
      break;
    }
  }
  if (idx >= savepoint_names_.size()) {
    return Status::InvalidArgument("savepoint not found");
  }
  const size_t keep = savepoints_[idx];
  while (journal_.size() > keep) {
    const TxnJournalEntry entry = journal_.back();
    journal_.pop_back();
    if (engine) {
      if (entry.had_value) {
        (void)engine->Put(entry.engine_key, entry.old_value, 0);
      } else {
        (void)engine->Delete(entry.engine_key, txn_id_);
      }
    }
  }
  savepoint_names_.resize(idx);
  savepoints_.resize(idx);
  return Status::Ok();
}

void TransactionState::RecordBeforePut(Engine* engine,
                                       const std::string& encoded_key) {
  if (!active_ || !engine) return;
  TxnJournalEntry entry{};
  entry.engine_key = encoded_key;
  std::string value;
  const Status gs = ReadKey(engine, encoded_key, &value);
  if (gs.ok()) {
    entry.had_value = true;
    entry.old_value = std::move(value);
  }
  journal_.push_back(std::move(entry));
}

void TransactionState::RecordBeforeDelete(Engine* engine,
                                          const std::string& encoded_key) {
  RecordBeforePut(engine, encoded_key);
}

}  // namespace sql
}  // namespace ebtree
