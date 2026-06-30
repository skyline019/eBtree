#pragma once

#include <unordered_map>
#include <utility>

#include "ebtree/common/config.h"
#include "ebtree/common/status.h"
#include "ebtree/concept/btree/btree.h"
#include "ebtree/concept/datafile/datafile.h"
#include "ebtree/concept/memtable/memtable.h"
#include "ebtree/concept/wal/wal.h"

namespace ebtree {

struct FlusherContext {
  WalWriter* wal{nullptr};
  MemTable* frozen{nullptr};
  DataFile* datafile{nullptr};
  BTreeIndex* btree{nullptr};
  std::unordered_map<std::string, std::pair<std::string, uint64_t>>* committed{
      nullptr};
  EngineStats* stats{nullptr};
  uint8_t generation{0};
};

class Flusher {
 public:
  static Status Flush(FlusherContext* ctx);
};

}  // namespace ebtree
