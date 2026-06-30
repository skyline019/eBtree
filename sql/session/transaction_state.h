#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace sql {

struct TxnJournalEntry {
  std::string engine_key;
  bool had_value{false};
  std::string old_value;
};

class TransactionState {
 public:
  bool active() const { return active_; }

  Status Begin();
  Status Commit(Engine* engine, DurabilityClass tier);
  Status Rollback(Engine* engine);
  Status Savepoint(const std::string& name);

  void RecordBeforePut(Engine* engine, const std::string& encoded_key);
  void RecordBeforeDelete(Engine* engine, const std::string& encoded_key);

 private:
  bool active_{false};
  std::vector<TxnJournalEntry> journal_;
  std::vector<size_t> savepoints_;
  std::vector<std::string> savepoint_names_;
};

}  // namespace sql
}  // namespace ebtree
