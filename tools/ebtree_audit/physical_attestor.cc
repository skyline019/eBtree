#include "physical_attestor.h"

#include <filesystem>
#include <fstream>

#include "ebtree/concept/datafile/datafile.h"
#include "ebtree/concept/superblock/superblock.h"
#include "ebtree/concept/tlog/tlog.h"
#include "ebtree/concept/wal/wal.h"
#include "ebtree/concept/wal/wal_key_index.h"

#include "digest.h"
#include "shard_paths.h"

namespace ebtree {
namespace audit {

namespace {

bool IsBadWalMarker(const std::string& wal_path) {
  std::ifstream in(wal_path, std::ios::binary);
  if (!in) return false;
  char marker[6]{};
  in.read(marker, 6);
  return in.gcount() == 6 && std::string(marker, 6) == "BADWAL";
}

struct WalScanStats {
  uint64_t record_count{0};
  uint64_t max_lsn{0};
  uint64_t unreplayed_tail_count{0};
};

WalScanStats ScanWal(const std::string& wal_path, uint64_t wal_lsn) {
  WalScanStats stats;
  std::ifstream in(wal_path, std::ios::binary);
  if (!in) return stats;
  if (IsBadWalMarker(wal_path)) return stats;

  while (in) {
    WalRecordHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in) break;
    in.seekg(hdr.key_len + hdr.value_len, std::ios::cur);
    if (!in) break;
    ++stats.record_count;
    stats.max_lsn = std::max(stats.max_lsn, hdr.lsn);
    if (hdr.lsn > wal_lsn) ++stats.unreplayed_tail_count;
  }
  return stats;
}

uint64_t CountDataFileRecords(const std::string& data_path) {
  std::ifstream in(data_path, std::ios::binary);
  if (!in) return 0;
  uint64_t count = 0;
  while (in) {
    DataRecordHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in) break;
    in.seekg(hdr.key_len + hdr.value_len, std::ios::cur);
    if (!in) break;
    ++count;
  }
  return count;
}

Status ReconstructCommittedFromDisk(const std::string& engine_path,
                                    uint32_t shard_id,
                                    const SuperBlock& sb, bool badwal,
                                    uint64_t* key_count_out) {
  const std::string data_path = ShardDataPath(engine_path, shard_id);
  const std::string wal_path = ShardWalPath(engine_path, shard_id);
  const std::string tlog_path = ShardTLogPath(engine_path, shard_id);

  std::unordered_map<std::string, std::pair<std::string, uint64_t>> committed;
  uint64_t max_lsn = 0;

  if (badwal) {
    TLogWriter tlog(tlog_path);
    TLogSnapshot snap{};
    (void)tlog.LatestSnapshot(&snap);
    DataFile df(data_path);
    if (snap.datafile_size > 0) {
      (void)df.LoadUpToByteOffset(snap.datafile_size, &committed, &max_lsn);
    } else {
      (void)df.LoadUpToLsn(sb.critical.data_lsn, &committed, &max_lsn);
    }
  } else {
    DataFile df(data_path);
    (void)df.LoadUpToLsn(sb.critical.data_lsn, &committed, &max_lsn);

    WalWriter wal(wal_path);
    const uint64_t after = sb.critical.wal_lsn;
    (void)wal.ReplayFrom(after, [&](WalOp op, const std::string& key,
                                    const std::string& value) -> Status {
      if (op == WalOp::kDelete) {
        committed.erase(key);
      } else {
        committed[key] = {value, max_lsn + 1};
      }
      return Status::Ok();
    });
  }

  if (key_count_out) *key_count_out = committed.size();
  return Status::Ok();
}

}  // namespace

Status PhysicalAttest(const std::string& engine_path, uint32_t shard_count,
                      PhysicalReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->shards.clear();
  out->shards.reserve(shard_count);

  for (uint32_t shard_id = 0; shard_id < shard_count; ++shard_id) {
    PhysicalShardReport shard_report{};
    shard_report.shard_id = shard_id;

    const std::string super_path = ShardSuperPath(engine_path, shard_id);
    const std::string wal_path = ShardWalPath(engine_path, shard_id);
    const std::string data_path = ShardDataPath(engine_path, shard_id);
    const std::string tlog_path = ShardTLogPath(engine_path, shard_id);

    SuperBlock sb{};
    SuperBlockStore store(super_path);
    const Status ls = store.Load(&sb);
    shard_report.superblock.valid = ls.ok();
    if (ls.ok()) {
      shard_report.superblock.epoch = sb.critical.epoch;
      shard_report.superblock.data_lsn = sb.critical.data_lsn;
      shard_report.superblock.wal_lsn = sb.critical.wal_lsn;
      shard_report.superblock.active_root = sb.critical.active_root;
      shard_report.superblock.tlog_tail = sb.tlog_tail;
      shard_report.superblock.active_slot =
          (sb.critical.epoch % 2 == 0) ? 0 : 1;
    }

    const bool badwal = IsBadWalMarker(wal_path);
    shard_report.wal.badwal_marker = badwal;
    const WalScanStats wal_stats =
        ScanWal(wal_path, shard_report.superblock.wal_lsn);
    shard_report.wal.record_count = wal_stats.record_count;
    shard_report.wal.max_lsn = wal_stats.max_lsn;
    shard_report.wal.unreplayed_tail_count = wal_stats.unreplayed_tail_count;

    DataFile df(data_path);
    uint64_t data_max = 0;
    std::unordered_map<std::string, std::pair<std::string, uint64_t>> tmp;
    (void)df.LoadAll(&tmp, &data_max);
    shard_report.datafile.max_lsn = data_max;
    shard_report.datafile.record_count = CountDataFileRecords(data_path);

    TLogReader tlog_reader(tlog_path);
    std::vector<TLogSnapshot> snaps;
    if (tlog_reader.ListSnapshots(&snaps).ok()) {
      shard_report.tlog.chain_length = snaps.size();
      if (!snaps.empty()) {
        const TLogSnapshot& latest = snaps.back();
        shard_report.tlog.latest_data_lsn = latest.data_lsn;
        shard_report.tlog.latest_datafile_size = latest.datafile_size;
      }
    }

    shard_report.invariants.data_lsn_le_wal_lsn =
        shard_report.superblock.valid &&
        shard_report.superblock.data_lsn <= shard_report.superblock.wal_lsn;

    shard_report.invariants.tlog_tail_valid = true;
    if (shard_report.superblock.tlog_tail > 0) {
      bool found = false;
      for (const auto& snap : snaps) {
        if (snap.file_offset == shard_report.superblock.tlog_tail) {
          found = true;
          break;
        }
      }
      shard_report.invariants.tlog_tail_valid = found;
    }

    if (std::filesystem::exists(super_path)) {
      shard_report.digests.super_sha256 = Sha256HexFile(super_path);
    }
    if (std::filesystem::exists(wal_path)) {
      shard_report.digests.wal_sha256 = Sha256HexFile(wal_path);
    }
    if (std::filesystem::exists(data_path)) {
      shard_report.digests.data_sha256 = Sha256HexFile(data_path);
    }
    if (std::filesystem::exists(tlog_path)) {
      shard_report.digests.tlog_sha256 = Sha256HexFile(tlog_path);
    }

    uint64_t key_count = 0;
    if (shard_report.superblock.valid) {
      (void)ReconstructCommittedFromDisk(engine_path, shard_id, sb, badwal,
                                         &key_count);
    }
    shard_report.reconstructed_key_count = key_count;

    out->shards.push_back(std::move(shard_report));
  }

  return Status::Ok();
}

}  // namespace audit
}  // namespace ebtree
