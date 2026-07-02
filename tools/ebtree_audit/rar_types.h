#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/config.h"
#include "ebtree/concept/recovery/recovery_state.h"

namespace ebtree {
namespace audit {

enum class ContractMode { kVisibility, kDurable };

enum class InferredRecoveryPath {
  kFastOpenDeferred,
  kWalReplayComplete,
  kTLogFallback,
  kLazyKey,
  kOnDiskLazy,
  kCommittedHot,
  kUnknown,
};

enum class RarVerdict { kPass, kWarn, kRefuseStart };

struct RarPolicy {
  uint64_t recovery_max_missing{0};
  bool allow_unexpected_keys{false};
  bool require_unexpected_path_zero{true};
  bool require_tier_consistent{false};
  uint64_t max_decompress_fail{0};
};

struct PhysicalSuperBlockInfo {
  int active_slot{-1};
  uint64_t epoch{0};
  uint64_t data_lsn{0};
  uint64_t wal_lsn{0};
  uint64_t active_root{0};
  uint64_t tlog_tail{0};
  bool valid{false};
};

struct PhysicalWalInfo {
  uint64_t record_count{0};
  uint64_t max_lsn{0};
  bool badwal_marker{false};
  uint64_t unreplayed_tail_count{0};
};

struct PhysicalDataFileInfo {
  uint64_t record_count{0};
  uint64_t max_lsn{0};
};

struct PhysicalTLogInfo {
  uint64_t chain_length{0};
  uint64_t latest_data_lsn{0};
  uint64_t latest_datafile_size{0};
};

struct PhysicalDigests {
  std::string super_sha256;
  std::string wal_sha256;
  std::string data_sha256;
  std::string tlog_sha256;
};

struct PhysicalInvariants {
  bool data_lsn_le_wal_lsn{false};
  bool tlog_tail_valid{false};
};

struct PhysicalShardReport {
  uint32_t shard_id{0};
  PhysicalSuperBlockInfo superblock{};
  PhysicalWalInfo wal{};
  PhysicalDataFileInfo datafile{};
  PhysicalTLogInfo tlog{};
  PhysicalDigests digests{};
  PhysicalInvariants invariants{};
  uint64_t reconstructed_key_count{0};
};

struct PhysicalReport {
  std::vector<PhysicalShardReport> shards;
};

struct KeyProbeResult {
  uint32_t shard{0};
  std::string key;
  bool found{false};
  std::string value_sha256;
  std::string read_tier;
};

struct CatalogSidecarReport {
  std::string path;
  uint32_t table_count{0};
  std::string key_set_source;
};

struct OpLogSidecarReport {
  std::string path;
  uint64_t entry_count{0};
  uint64_t durable_entry_count{0};
  uint64_t pending_count{0};
  std::string key_set_source;
};

struct RecoveryShardReport {
  uint32_t shard_id{0};
  std::string state;
  InferredRecoveryPath inferred_path{InferredRecoveryPath::kUnknown};
  std::unordered_map<std::string, uint64_t> read_tier_hits;
};

struct RecoveryReport {
  std::string recovery_mode;
  bool wal_replay_pending{false};
  InferredRecoveryPath inferred_path{InferredRecoveryPath::kUnknown};
  uint64_t unexpected_path_total{0};
  uint64_t stable_lsn{0};
  std::vector<RecoveryShardReport> shard_state;
  std::vector<KeyProbeResult> probes;
};

struct ContractKeyIssue {
  uint32_t shard{0};
  std::string key;
  std::string expected_hash;
  std::string actual_hash;
};

struct ContractReport {
  ContractMode mode{ContractMode::kVisibility};
  std::string key_set_source;
  uint64_t durable_expected_count{0};
  uint64_t recovered_count{0};
  std::vector<ContractKeyIssue> missing;
  std::vector<ContractKeyIssue> unexpected;
  std::vector<ContractKeyIssue> pending_uncommitted;
};

struct ExpectedKeyEntry {
  uint32_t shard{0};
  std::string key;
  std::string value;
  std::string expected_value_hash;
  bool in_durable_set{true};
  bool in_visibility_set{true};
};

struct ExpectSnapshot {
  ContractMode mode{ContractMode::kVisibility};
  std::string key_set_source;
  std::vector<ExpectedKeyEntry> entries;
  std::vector<std::string> touched_keys;
};

struct KernelSectionReport {
  uint64_t checkpoint_lsn{0};
  uint64_t pages_touched{0};
  uint64_t unexpected_path_total{0};
  uint64_t stable_lsn{0};
  std::string recovery_mode;
  std::string inferred_path;
  uint64_t compress_raw_total{0};
  uint64_t compress_bytes_saved{0};
  uint64_t decompress_fail{0};
  std::vector<std::string> forbidden_violations;
};

struct TierConsistencyIssue {
  uint32_t shard{0};
  std::string recovery_state;
  std::string probe_key;
  std::string expected_tier;
  std::string actual_tier;
};

struct TierContractSection {
  bool consistent{true};
  std::vector<TierConsistencyIssue> issues;
};

struct SidecarChainSection {
  uint64_t sequence{0};
  std::string prev_rar_sha256;
  std::string rar_sha256;
  std::string op_log_head_sha256;
};

struct RarReport {
  std::string rar_version{"1.0"};
  int64_t generated_at_unix{0};
  std::string engine_path;
  DurabilityClass durability_tier{DurabilityClass::kBalanced};
  uint32_t shard_count{1};
  PhysicalReport physical{};
  RecoveryReport recovery{};
  ContractReport contract{};
  RarPolicy policy{};
  RarVerdict verdict{RarVerdict::kPass};
  std::string verdict_reason;
  CatalogSidecarReport catalog{};
  OpLogSidecarReport op_log{};
  KernelSectionReport kernel{};
  TierContractSection tier_contract{};
  SidecarChainSection sidecar_chain{};
  std::string signature;
};

std::string DurabilityClassToString(DurabilityClass tier);
DurabilityClass DurabilityClassFromString(const std::string& s);
std::string ContractModeToString(ContractMode mode);
std::string InferredRecoveryPathToString(InferredRecoveryPath path);
std::string RarVerdictToString(RarVerdict verdict);

}  // namespace audit
}  // namespace ebtree
