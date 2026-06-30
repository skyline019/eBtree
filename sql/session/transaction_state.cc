#include "transaction_state.h"

namespace ebtree {
namespace sql {

Status TransactionState::Begin() {
  if (active_) {
    return Status::InvalidArgument("transaction already active");
  }
  active_ = true;
  journal_.clear();
  savepoints_.clear();
  savepoint_names_.clear();
  return Status::Ok();
}

Status TransactionState::Commit(Engine* engine, DurabilityClass tier) {
  if (!active_) {
    return Status::InvalidArgument("no active transaction");
  }
  active_ = false;
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
    for (auto it = journal_.rbegin(); it != journal_.rend(); ++it) {
      if (it->had_value) {
        (void)engine->Put(it->engine_key, it->old_value);
      } else {
        (void)engine->Delete(it->engine_key);
      }
    }
  }
  active_ = false;
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

void TransactionState::RecordBeforePut(Engine* engine,
                                       const std::string& encoded_key) {
  if (!active_ || !engine) return;
  for (const auto& e : journal_) {
    if (e.engine_key == encoded_key) return;
  }
  TxnJournalEntry entry{};
  entry.engine_key = encoded_key;
  std::string value;
  const Status st = engine->Get(encoded_key, &value);
  if (st.ok()) {
    entry.had_value = true;
    entry.old_value = value;
  }
  journal_.push_back(std::move(entry));
}

void TransactionState::RecordBeforeDelete(Engine* engine,
                                            const std::string& encoded_key) {
  RecordBeforePut(engine, encoded_key);
}

}  // namespace sql
}  // namespace ebtree
