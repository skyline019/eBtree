#include "ebtree/concept/wal/txidx.h"

#include <algorithm>
#include <fstream>

namespace ebtree {

namespace {

constexpr uint64_t kTxidxMagic = 0xEB545844580031ULL;

}  // namespace

std::string TxnSidecarStore::PathForShard(const std::string& engine_path,
                                          uint32_t shard_id) {
  return engine_path + "/shard" + std::to_string(shard_id) + ".txidx";
}

TxnSidecarEntry* TxnSidecarStore::Find(uint32_t txn_id) {
  for (auto& entry : entries_) {
    if (entry.txn_id == txn_id) return &entry;
  }
  return nullptr;
}

void TxnSidecarStore::SetOpenTxn(uint32_t txn_id, uint64_t snapshot_lsn) {
  if (txn_id == 0) return;
  if (auto* existing = Find(txn_id)) {
    existing->snapshot_lsn = snapshot_lsn;
    existing->committed = false;
    return;
  }
  entries_.push_back(TxnSidecarEntry{txn_id, snapshot_lsn, false, 0});
}

void TxnSidecarStore::MarkCommitted(uint32_t txn_id, uint64_t commit_lsn) {
  if (txn_id == 0) return;
  TxnSidecarEntry* entry = Find(txn_id);
  if (!entry) {
    entries_.push_back(TxnSidecarEntry{txn_id, 0, true, commit_lsn});
  } else {
    entry->committed = true;
    entry->commit_lsn = commit_lsn;
  }
  last_commit_lsn_ = std::max(last_commit_lsn_, commit_lsn);
}

void TxnSidecarStore::MarkAborted(uint32_t txn_id) {
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [txn_id](const TxnSidecarEntry& e) {
                                  return e.txn_id == txn_id;
                                }),
                 entries_.end());
}

Status TxnSidecarStore::SaveToFile(const std::string& path) const {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return Status::IoError("txidx save open failed");
  out.write(reinterpret_cast<const char*>(&kTxidxMagic), sizeof(kTxidxMagic));
  const uint32_t count = static_cast<uint32_t>(entries_.size());
  out.write(reinterpret_cast<const char*>(&count), sizeof(count));
  out.write(reinterpret_cast<const char*>(&last_commit_lsn_),
            sizeof(last_commit_lsn_));
  for (const auto& entry : entries_) {
    out.write(reinterpret_cast<const char*>(&entry.txn_id), sizeof(entry.txn_id));
    out.write(reinterpret_cast<const char*>(&entry.snapshot_lsn),
              sizeof(entry.snapshot_lsn));
    const uint8_t committed = entry.committed ? 1 : 0;
    out.write(reinterpret_cast<const char*>(&committed), sizeof(committed));
    out.write(reinterpret_cast<const char*>(&entry.commit_lsn),
              sizeof(entry.commit_lsn));
  }
  return Status::Ok();
}

Status TxnSidecarStore::LoadFromFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::NotFound("txidx missing");
  uint64_t magic = 0;
  in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  if (!in || magic != kTxidxMagic) return Status::Corrupt("txidx bad magic");
  uint32_t count = 0;
  in.read(reinterpret_cast<char*>(&count), sizeof(count));
  in.read(reinterpret_cast<char*>(&last_commit_lsn_), sizeof(last_commit_lsn_));
  entries_.clear();
  entries_.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    TxnSidecarEntry entry{};
    in.read(reinterpret_cast<char*>(&entry.txn_id), sizeof(entry.txn_id));
    in.read(reinterpret_cast<char*>(&entry.snapshot_lsn),
            sizeof(entry.snapshot_lsn));
    uint8_t committed = 0;
    in.read(reinterpret_cast<char*>(&committed), sizeof(committed));
    entry.committed = committed != 0;
    in.read(reinterpret_cast<char*>(&entry.commit_lsn), sizeof(entry.commit_lsn));
    if (!in) return Status::Corrupt("txidx truncated");
    entries_.push_back(entry);
  }
  return Status::Ok();
}

}  // namespace ebtree
