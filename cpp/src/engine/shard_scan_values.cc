#include "ebtree/engine/shard_engine.h"

#include <algorithm>
#include <optional>

#include "ebtree/concept/btree/btree.h"
#include "ebtree/engine/snapshot_resolver.h"

namespace ebtree {

namespace {

std::optional<MemTableEntry> LookupMemTable(const MemTable& active,
                                            const MemTable& immutable,
                                            const MemTable& flushing,
                                            const std::string& key) {
  if (auto mt = active.Get(key)) return mt;
  if (auto mt = immutable.Get(key)) return mt;
  return flushing.Get(key);
}

uint64_t EffectiveHitLsn(ShardEngine& shard, const std::string& key,
                         uint64_t hit_lsn, const SnapshotReadContext& ctx) {
  if (ctx.snapshot_lsn == 0 || hit_lsn <= ctx.snapshot_lsn) return hit_lsn;
  if (!shard.vcs()) return 0;
  return shard.vcs()->Floor(key, ctx.snapshot_lsn);
}

}  // namespace

Status ShardEngine::ResolveScanValues(
    const std::vector<std::pair<std::string, uint64_t>>& hits,
    uint64_t snapshot_lsn,
    std::vector<std::pair<std::string, std::string>>* rows) {
  SnapshotReadContext ctx{snapshot_lsn, 0};
  return ResolveScanValues(hits, ctx, rows);
}

Status ShardEngine::ResolveScanValues(
    const std::vector<std::pair<std::string, uint64_t>>& hits,
    const SnapshotReadContext& ctx,
    std::vector<std::pair<std::string, std::string>>* rows) {
  if (!rows) return Status::InvalidArgument("rows is null");
  rows->clear();
  rows->reserve(hits.size());
  const bool disk_values =
      opts_.lazy_committed_load || btree_.on_disk_mode();
  const bool direct_disk_scan =
      disk_values && MemTablesEmpty() && committed_.empty();

  if (direct_disk_scan) {
    struct DiskEntry {
      uint64_t offset;
      uint64_t lsn;
      size_t row_idx;
    };
    std::vector<DiskEntry> disk_batch;
    disk_batch.reserve(hits.size());
    for (const auto& hit : hits) {
      const uint64_t lsn = EffectiveHitLsn(*this, hit.first, hit.second, ctx);
      if (lsn == 0) continue;
      uint64_t offset = 0;
      if (!datafile_->lsn_index().Lookup(lsn, &offset)) continue;
      const size_t row_idx = rows->size();
      rows->emplace_back(hit.first, std::string{});
      disk_batch.push_back({offset, lsn, row_idx});
    }
    if (!disk_batch.empty()) {
      std::sort(disk_batch.begin(), disk_batch.end(),
                [](const DiskEntry& a, const DiskEntry& b) {
                  return a.offset < b.offset;
                });
      std::vector<std::string> disk_values_vec(rows->size());
      const uint8_t reclaim =
          gc_ ? gc_->reclaim_generation() : static_cast<uint8_t>(0xFF);
      std::vector<std::pair<uint64_t, size_t>> batch_pairs;
      batch_pairs.reserve(disk_batch.size());
      for (const auto& entry : disk_batch) {
        batch_pairs.emplace_back(entry.offset, entry.row_idx);
      }
      const Status batch =
          datafile_reader_.ReadBatch(batch_pairs, &disk_values_vec, reclaim);
      if (batch.ok()) {
        for (const auto& entry : disk_batch) {
          if (entry.row_idx < rows->size() &&
              !disk_values_vec[entry.row_idx].empty()) {
            (*rows)[entry.row_idx].second =
                std::move(disk_values_vec[entry.row_idx]);
          }
        }
      } else {
        for (const auto& entry : disk_batch) {
          std::string value;
          if (ReadValueByLsn(entry.lsn, &value).ok()) {
            (*rows)[entry.row_idx].second = std::move(value);
          }
        }
      }
    }
    rows->erase(std::remove_if(rows->begin(), rows->end(),
                               [](const auto& row) { return row.second.empty(); }),
                rows->end());
    return Status::Ok();
  }

  if (!disk_values && !committed_.empty()) {
    for (const auto& hit : hits) {
      if (auto mt = LookupMemTable(memtable_, immutable_, flushing_, hit.first)) {
        if (mt->deleted) continue;
        if (!MemEntryVisible(*mt, ctx)) continue;
        rows->emplace_back(hit.first, mt->value);
        continue;
      }
      const auto it = committed_.find(hit.first);
      if (it == committed_.end()) continue;
      if (ctx.snapshot_lsn > 0 && it->second.second > ctx.snapshot_lsn) {
        const uint64_t floor_lsn =
            vcs_ ? vcs_->Floor(hit.first, ctx.snapshot_lsn) : 0;
        if (floor_lsn == 0) continue;
        std::string value;
        if (ReadValueByLsn(floor_lsn, &value).ok()) {
          RecordTier(ReadTier::kVersionChain);
          rows->emplace_back(hit.first, std::move(value));
        }
        continue;
      }
      rows->emplace_back(hit.first, it->second.first);
    }
    return Status::Ok();
  }

  std::vector<std::pair<std::string, std::string>> partial;
  partial.reserve(hits.size());
  struct DiskEntry {
    uint64_t offset;
    size_t partial_idx;
    uint64_t lsn;
  };
  std::vector<DiskEntry> disk_batch;
  disk_batch.reserve(hits.size());

  for (const auto& hit : hits) {
    if (!direct_disk_scan) {
      std::string value;
      const Status rs = ReadVisible(hit.first, &value, ctx);
      if (rs.ok()) {
        partial.emplace_back(hit.first, std::move(value));
        continue;
      }
    }
    const uint64_t lsn = EffectiveHitLsn(*this, hit.first, hit.second, ctx);
    if (lsn == 0) continue;
    if (!disk_values) continue;
    uint64_t offset = 0;
    if (!datafile_->lsn_index().Lookup(lsn, &offset)) continue;
    const size_t row_idx = partial.size();
    partial.emplace_back(hit.first, std::string{});
    disk_batch.push_back({offset, row_idx, lsn});
  }

  if (!disk_batch.empty()) {
    std::sort(disk_batch.begin(), disk_batch.end(),
              [](const DiskEntry& a, const DiskEntry& b) {
                return a.offset < b.offset;
              });
    std::vector<std::string> disk_values_vec(partial.size());
    const uint8_t reclaim =
        gc_ ? gc_->reclaim_generation() : static_cast<uint8_t>(0xFF);
    std::vector<std::pair<uint64_t, size_t>> batch_pairs;
    batch_pairs.reserve(disk_batch.size());
    for (const auto& entry : disk_batch) {
      batch_pairs.emplace_back(entry.offset, entry.partial_idx);
    }
    const Status batch =
        datafile_reader_.ReadBatch(batch_pairs, &disk_values_vec, reclaim);
    if (batch.ok()) {
      for (const auto& entry : disk_batch) {
        if (entry.partial_idx < partial.size() &&
            !disk_values_vec[entry.partial_idx].empty()) {
          partial[entry.partial_idx].second =
              std::move(disk_values_vec[entry.partial_idx]);
        }
      }
    } else {
      for (const auto& entry : disk_batch) {
        std::string value;
        if (ReadValueByLsn(entry.lsn, &value).ok()) {
          partial[entry.partial_idx].second = std::move(value);
        }
      }
    }
  }

  for (auto& row : partial) {
    if (!row.second.empty()) rows->push_back(std::move(row));
  }
  return Status::Ok();
}

}  // namespace ebtree
