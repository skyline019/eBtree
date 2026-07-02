#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"
#include "ebtree/engine/snapshot.h"

namespace ebtree {
namespace sql {

struct TxnJournalEntry {
  std::string engine_key;
  bool had_value{false};
  std::string old_value;
};

struct RangeTicket {
  uint64_t snapshot_lsn{0};
  std::string key_lo;
  std::string key_hi;
};

class TransactionState {
 public:
  bool active() const { return active_; }
  uint32_t txn_id() const { return txn_id_; }
  const SnapshotToken& snapshot() const { return snapshot_token_; }

  Status Begin(Engine* engine);
  Status Commit(Engine* engine, DurabilityClass tier);
  Status Rollback(Engine* engine);
  Status Savepoint(const std::string& name);
  Status ReleaseSavepoint(const std::string& name);
  Status RollbackToSavepoint(Engine* engine, const std::string& name);

  Status ReadKey(Engine* engine, const std::string& encoded_key,
                 std::string* value);
  void RecordWriteIntent(Engine* engine, const std::string& encoded_key);
  void RecordReadSample(Engine* engine, const std::string& encoded_key);
  void RegisterRangeScan(Engine* engine, const std::string& key_lo,
                         const std::string& key_hi);

  void RecordBeforePut(Engine* engine, const std::string& encoded_key);
  void RecordBeforeDelete(Engine* engine, const std::string& encoded_key);

 private:
  static uint32_t AllocateTxnId();

  Status ValidateCommitOCC(Engine* engine) const;
  Status SampleReadVersion(Engine* engine, const std::string& encoded_key,
                           uint64_t* lsn_out);

  bool active_{false};
  uint32_t txn_id_{0};
  SnapshotToken snapshot_token_{};
  std::unordered_map<std::string, uint64_t> read_set_;
  std::unordered_map<std::string, uint64_t> write_set_;
  std::vector<RangeTicket> range_tickets_;
  std::vector<TxnJournalEntry> journal_;
  std::vector<size_t> savepoints_;
  std::vector<std::string> savepoint_names_;
};

}  // namespace sql
}  // namespace ebtree
