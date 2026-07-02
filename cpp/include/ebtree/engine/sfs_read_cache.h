#pragma once

#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace ebtree {

class SfsReadCache {
 public:
  explicit SfsReadCache(size_t capacity = 16384);

  std::optional<uint64_t> Lookup(uint64_t key_hash, uint64_t s_epoch) const;
  void Insert(uint64_t key_hash, uint64_t s_epoch, uint64_t floor_lsn,
              bool tombstone);
  void InvalidateGeneration(uint8_t reclaim_generation);

 private:
  struct Entry {
    uint64_t key_hash{0};
    uint64_t s_epoch{0};
    uint64_t floor_lsn{0};
    bool tombstone{false};
    uint8_t reclaim_generation{0xFF};
  };

  size_t capacity_;
  mutable std::mutex mu_;
  std::list<Entry> lru_;
  std::unordered_map<uint64_t, std::list<Entry>::iterator> index_;
};

inline uint64_t SfsEpoch(uint64_t snapshot_lsn, uint64_t checkpoint_lsn) {
  if (checkpoint_lsn == 0) return snapshot_lsn;
  return snapshot_lsn - (snapshot_lsn % checkpoint_lsn);
}

}  // namespace ebtree
