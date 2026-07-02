#pragma once

#include <string>
#include <utility>
#include <vector>

#include "ebtree/engine/engine.h"
#include "sql/session/transaction_state.h"

namespace ebtree {
namespace sql {

inline Status TxnGet(Engine* engine, TransactionState* txn, const std::string& key,
                   std::string* value) {
  if (!engine || !value) return Status::InvalidArgument("null engine or value");
  if (txn && txn->active()) {
    return txn->ReadKey(engine, key, value);
  }
  return engine->Get(key, value);
}

inline Status TxnScan(Engine* engine, TransactionState* txn,
                      const TypedPlan& plan,
                      std::vector<std::pair<std::string, std::string>>* rows) {
  if (!engine || !rows) return Status::InvalidArgument("null engine or rows");
  if (txn && txn->active()) {
    return engine->ScanAtSnapshot(plan, txn->snapshot(), txn->txn_id(), rows);
  }
  return engine->Scan(plan, rows);
}

}  // namespace sql
}  // namespace ebtree
