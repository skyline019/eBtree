#pragma once

#include <gtest/gtest.h>

#include <optional>
#include <unordered_map>
#include <vector>

#include "ebtree/engine/engine.h"
#include "ebtree/engine/snapshot.h"

namespace ebtree {
namespace test {

struct SnapshotVersion {
  uint64_t lsn{0};
  std::string value;
  bool deleted{false};
};

class SnapshotOracle {
 public:
  void OnPut(uint64_t lsn, const std::string& key, const std::string& value) {
    history_[key].push_back(SnapshotVersion{lsn, value, false});
  }

  void OnDelete(uint64_t lsn, const std::string& key) {
    history_[key].push_back(SnapshotVersion{lsn, "", true});
  }

  std::optional<std::string> ValueAt(const std::string& key,
                                     uint64_t snapshot_lsn) const {
    const auto it = history_.find(key);
    if (it == history_.end()) return std::nullopt;
    const SnapshotVersion* best = nullptr;
    for (const auto& ver : it->second) {
      if (ver.lsn <= snapshot_lsn &&
          (!best || ver.lsn >= best->lsn)) {
        best = &ver;
      }
    }
    if (!best) return std::nullopt;
    if (best->deleted) return std::nullopt;
    return best->value;
  }

  ::testing::AssertionResult VerifyEngineAtSnapshot(
      Engine* engine, const SnapshotToken& token,
      uint32_t reader_txn_id) const {
    if (!engine) {
      return ::testing::AssertionFailure() << "null engine";
    }
    for (const auto& kv : history_) {
      std::string value;
      const Status st =
          engine->GetAtSnapshot(kv.first, token, reader_txn_id, &value);
      const auto expected = ValueAt(kv.first, token.ForShard(0));
      if (!expected.has_value()) {
        if (st.ok()) {
          return ::testing::AssertionFailure()
                 << "unexpected value for " << kv.first;
        }
        continue;
      }
      if (!st.ok()) {
        return ::testing::AssertionFailure()
               << "missing key " << kv.first << " status=" << st.message();
      }
      if (value != *expected) {
        return ::testing::AssertionFailure()
               << "key " << kv.first << " expected " << *expected << " got "
               << value;
      }
    }
    return ::testing::AssertionSuccess();
  }

 private:
  std::unordered_map<std::string, std::vector<SnapshotVersion>> history_;
};

}  // namespace test
}  // namespace ebtree
