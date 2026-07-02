#include "ebtree/engine/sfs_read_cache.h"

namespace ebtree {

namespace {

uint64_t CacheKey(uint64_t key_hash, uint64_t s_epoch) {
  return (key_hash << 32) ^ s_epoch;
}

}  // namespace

SfsReadCache::SfsReadCache(size_t capacity) : capacity_(capacity) {}

std::optional<uint64_t> SfsReadCache::Lookup(uint64_t key_hash,
                                             uint64_t s_epoch) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = index_.find(CacheKey(key_hash, s_epoch));
  if (it == index_.end()) return std::nullopt;
  if ((*it->second).tombstone) return static_cast<uint64_t>(0);
  return (*it->second).floor_lsn;
}

void SfsReadCache::Insert(uint64_t key_hash, uint64_t s_epoch, uint64_t floor_lsn,
                          bool tombstone) {
  std::lock_guard<std::mutex> lock(mu_);
  const uint64_t ck = CacheKey(key_hash, s_epoch);
  const auto existing = index_.find(ck);
  if (existing != index_.end()) {
    lru_.erase(existing->second);
    index_.erase(existing);
  }
  lru_.push_front(Entry{key_hash, s_epoch, floor_lsn, tombstone, 0xFF});
  index_[ck] = lru_.begin();
  while (index_.size() > capacity_) {
    const Entry back = lru_.back();
    index_.erase(CacheKey(back.key_hash, back.s_epoch));
    lru_.pop_back();
  }
}

void SfsReadCache::InvalidateGeneration(uint8_t reclaim_generation) {
  std::lock_guard<std::mutex> lock(mu_);
  for (auto it = lru_.begin(); it != lru_.end();) {
    if (it->reclaim_generation == reclaim_generation) {
      index_.erase(CacheKey(it->key_hash, it->s_epoch));
      it = lru_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace ebtree
