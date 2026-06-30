#pragma once

#include <sstream>
#include <string>

#include "rar_types.h"

namespace ebtree {
namespace audit {

namespace {

void JsonEscape(std::ostream& out, const std::string& s) {
  out << '"';
  for (char c : s) {
    switch (c) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << c;
        break;
    }
  }
  out << '"';
}

void WriteIndent(std::ostream& out, int depth) {
  for (int i = 0; i < depth; ++i) out << "  ";
}

void WriteKeyIssues(std::ostream& out, int depth,
                    const std::vector<ContractKeyIssue>& issues) {
  WriteIndent(out, depth);
  out << "[\n";
  for (size_t i = 0; i < issues.size(); ++i) {
    const auto& issue = issues[i];
    WriteIndent(out, depth + 1);
    out << "{\n";
    WriteIndent(out, depth + 2);
    out << "\"shard\": " << issue.shard << ",\n";
    WriteIndent(out, depth + 2);
    out << "\"key\": ";
    JsonEscape(out, issue.key);
    out << ",\n";
    if (!issue.expected_hash.empty()) {
      WriteIndent(out, depth + 2);
      out << "\"expected_hash\": ";
      JsonEscape(out, issue.expected_hash);
      out << ",\n";
    }
    if (!issue.actual_hash.empty()) {
      WriteIndent(out, depth + 2);
      out << "\"actual_hash\": ";
      JsonEscape(out, issue.actual_hash);
      out << "\n";
    }
    WriteIndent(out, depth + 1);
    out << "}";
    if (i + 1 < issues.size()) out << ",";
    out << "\n";
  }
  WriteIndent(out, depth);
  out << "]";
}

}  // namespace

void WriteRarReportJson(const RarReport& report, std::ostream& out) {
  out << "{\n";
  out << "  \"rar_version\": ";
  JsonEscape(out, report.rar_version);
  out << ",\n";
  if (report.rar_version >= "2.0") {
    out << "  \"attest_export_version\": 1,\n";
  }
  out << "  \"generated_at_unix\": " << report.generated_at_unix << ",\n";
  out << "  \"engine_path\": ";
  JsonEscape(out, report.engine_path);
  out << ",\n";
  out << "  \"durability_tier\": ";
  JsonEscape(out, DurabilityClassToString(report.durability_tier));
  out << ",\n";
  out << "  \"shard_count\": " << report.shard_count << ",\n";

  out << "  \"physical\": {\n";
  out << "    \"shards\": [\n";
  for (size_t si = 0; si < report.physical.shards.size(); ++si) {
    const auto& shard = report.physical.shards[si];
    out << "      {\n";
    out << "        \"shard_id\": " << shard.shard_id << ",\n";
    out << "        \"superblock\": {\n";
    out << "          \"active_slot\": " << shard.superblock.active_slot << ",\n";
    out << "          \"epoch\": " << shard.superblock.epoch << ",\n";
    out << "          \"data_lsn\": " << shard.superblock.data_lsn << ",\n";
    out << "          \"wal_lsn\": " << shard.superblock.wal_lsn << ",\n";
    out << "          \"active_root\": " << shard.superblock.active_root << ",\n";
    out << "          \"tlog_tail\": " << shard.superblock.tlog_tail << ",\n";
    out << "          \"valid\": " << (shard.superblock.valid ? "true" : "false")
        << "\n";
    out << "        },\n";
    out << "        \"wal\": {\n";
    out << "          \"record_count\": " << shard.wal.record_count << ",\n";
    out << "          \"max_lsn\": " << shard.wal.max_lsn << ",\n";
    out << "          \"badwal_marker\": "
        << (shard.wal.badwal_marker ? "true" : "false") << ",\n";
    out << "          \"unreplayed_tail_count\": "
        << shard.wal.unreplayed_tail_count << "\n";
    out << "        },\n";
    out << "        \"datafile\": {\n";
    out << "          \"record_count\": " << shard.datafile.record_count << ",\n";
    out << "          \"max_lsn\": " << shard.datafile.max_lsn << "\n";
    out << "        },\n";
    out << "        \"tlog\": {\n";
    out << "          \"chain_length\": " << shard.tlog.chain_length << ",\n";
    out << "          \"latest_data_lsn\": " << shard.tlog.latest_data_lsn
        << ",\n";
    out << "          \"latest_datafile_size\": "
        << shard.tlog.latest_datafile_size << "\n";
    out << "        },\n";
    out << "        \"digests\": {\n";
    out << "          \"super_sha256\": ";
    JsonEscape(out, shard.digests.super_sha256);
    out << ",\n";
    out << "          \"wal_sha256\": ";
    JsonEscape(out, shard.digests.wal_sha256);
    out << ",\n";
    out << "          \"data_sha256\": ";
    JsonEscape(out, shard.digests.data_sha256);
    out << ",\n";
    out << "          \"tlog_sha256\": ";
    JsonEscape(out, shard.digests.tlog_sha256);
    out << "\n";
    out << "        },\n";
    out << "        \"invariants\": {\n";
    out << "          \"data_lsn_le_wal_lsn\": "
        << (shard.invariants.data_lsn_le_wal_lsn ? "true" : "false") << ",\n";
    out << "          \"tlog_tail_valid\": "
        << (shard.invariants.tlog_tail_valid ? "true" : "false") << "\n";
    out << "        },\n";
    out << "        \"reconstructed_key_count\": "
        << shard.reconstructed_key_count << "\n";
    out << "      }";
    if (si + 1 < report.physical.shards.size()) out << ",";
    out << "\n";
  }
  out << "    ]\n";
  out << "  },\n";

  out << "  \"recovery\": {\n";
  out << "    \"recovery_mode\": ";
  JsonEscape(out, report.recovery.recovery_mode);
  out << ",\n";
  out << "    \"wal_replay_pending\": "
      << (report.recovery.wal_replay_pending ? "true" : "false") << ",\n";
  out << "    \"inferred_path\": ";
  JsonEscape(out, InferredRecoveryPathToString(report.recovery.inferred_path));
  out << ",\n";
  out << "    \"unexpected_path_total\": "
      << report.recovery.unexpected_path_total << ",\n";
  out << "    \"stable_lsn\": " << report.recovery.stable_lsn << ",\n";
  out << "    \"shard_state\": [\n";
  for (size_t i = 0; i < report.recovery.shard_state.size(); ++i) {
    const auto& sr = report.recovery.shard_state[i];
    out << "      {\n";
    out << "        \"shard_id\": " << sr.shard_id << ",\n";
    out << "        \"state\": ";
    JsonEscape(out, sr.state);
    out << ",\n";
    out << "        \"inferred_path\": ";
    JsonEscape(out, InferredRecoveryPathToString(sr.inferred_path));
    out << ",\n";
    out << "        \"read_tier_hits\": {";
    bool first = true;
    for (const auto& kv : sr.read_tier_hits) {
      if (!first) out << ",";
      first = false;
      out << "\n          ";
      JsonEscape(out, kv.first);
      out << ": " << kv.second;
    }
    if (!sr.read_tier_hits.empty()) out << "\n        ";
    out << "}\n";
    out << "      }";
    if (i + 1 < report.recovery.shard_state.size()) out << ",";
    out << "\n";
  }
  out << "    ],\n";
  out << "    \"probes\": [\n";
  for (size_t i = 0; i < report.recovery.probes.size(); ++i) {
    const auto& p = report.recovery.probes[i];
    out << "      {\n";
    out << "        \"shard\": " << p.shard << ",\n";
    out << "        \"key\": ";
    JsonEscape(out, p.key);
    out << ",\n";
    out << "        \"found\": " << (p.found ? "true" : "false") << ",\n";
    out << "        \"value_sha256\": ";
    JsonEscape(out, p.value_sha256);
    out << ",\n";
    out << "        \"read_tier\": ";
    JsonEscape(out, p.read_tier);
    out << "\n";
    out << "      }";
    if (i + 1 < report.recovery.probes.size()) out << ",";
    out << "\n";
  }
  out << "    ]\n";
  out << "  },\n";

  out << "  \"contract\": {\n";
  out << "    \"mode\": ";
  JsonEscape(out, ContractModeToString(report.contract.mode));
  out << ",\n";
  out << "    \"key_set_source\": ";
  JsonEscape(out, report.contract.key_set_source);
  out << ",\n";
  out << "    \"durable_expected_count\": "
      << report.contract.durable_expected_count << ",\n";
  out << "    \"recovered_count\": " << report.contract.recovered_count
      << ",\n";
  out << "    \"missing\": ";
  WriteKeyIssues(out, 2, report.contract.missing);
  out << ",\n";
  out << "    \"unexpected\": ";
  WriteKeyIssues(out, 2, report.contract.unexpected);
  out << ",\n";
  out << "    \"pending_uncommitted\": ";
  WriteKeyIssues(out, 2, report.contract.pending_uncommitted);
  out << "\n";
  out << "  },\n";

  out << "  \"policy\": {\n";
  out << "    \"recovery_max_missing\": " << report.policy.recovery_max_missing
      << ",\n";
  out << "    \"allow_unexpected_keys\": "
      << (report.policy.allow_unexpected_keys ? "true" : "false") << ",\n";
  out << "    \"require_unexpected_path_zero\": "
      << (report.policy.require_unexpected_path_zero ? "true" : "false")
      << "\n";
  out << "  },\n";

  if (!report.catalog.path.empty()) {
    out << "  \"catalog\": {\n";
    out << "    \"path\": ";
    JsonEscape(out, report.catalog.path);
    out << ",\n";
    out << "    \"table_count\": " << report.catalog.table_count << ",\n";
    out << "    \"key_set_source\": ";
    JsonEscape(out, report.catalog.key_set_source);
    out << "\n  },\n";
  }

  if (!report.op_log.path.empty()) {
    out << "  \"op_log\": {\n";
    out << "    \"path\": ";
    JsonEscape(out, report.op_log.path);
    out << ",\n";
    out << "    \"entry_count\": " << report.op_log.entry_count << ",\n";
    out << "    \"durable_entry_count\": " << report.op_log.durable_entry_count
        << ",\n";
    out << "    \"pending_count\": " << report.op_log.pending_count << ",\n";
    out << "    \"key_set_source\": ";
    JsonEscape(out, report.op_log.key_set_source);
    out << "\n  },\n";
  }

  out << "  \"verdict\": ";
  JsonEscape(out, RarVerdictToString(report.verdict));
  out << ",\n";
  out << "  \"verdict_reason\": ";
  JsonEscape(out, report.verdict_reason);
  if (!report.signature.empty()) {
    out << ",\n  \"signature\": ";
    JsonEscape(out, report.signature);
  }
  out << "\n}\n";
}

std::string RarReportToJson(const RarReport& report) {
  std::ostringstream oss;
  WriteRarReportJson(report, oss);
  return oss.str();
}

}  // namespace audit
}  // namespace ebtree
