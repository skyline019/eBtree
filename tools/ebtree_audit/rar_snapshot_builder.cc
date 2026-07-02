#include "rar_snapshot_builder.h"

#include <sstream>

namespace ebtree {
namespace audit {

namespace {

void JsonEscape(std::ostream& out, const std::string& s) {
  out << '"';
  for (char c : s) {
    if (c == '"') out << "\\\"";
    else if (c == '\\') out << "\\\\";
    else out << c;
  }
  out << '"';
}

void WriteTierHits(std::ostream& out, const RecoveryShardSnapshot& shard) {
  out << "[";
  bool first = true;
  for (size_t t = 0; t < kReadTierCount; ++t) {
    if (shard.read_tier_hits[t] == 0) continue;
    if (!first) out << ",";
    first = false;
    out << "{";
    JsonEscape(out, ReadTierToString(static_cast<ReadTier>(t)));
    out << ":" << shard.read_tier_hits[t] << "}";
  }
  out << "]";
}

void WriteKernelJson(std::ostream& out, const AttestExportReportV2& report) {
  out << "{";
  out << "\"checkpoint_lsn\":" << report.checkpoint_lsn << ",";
  out << "\"pages_touched\":" << report.pages_touched << ",";
  out << "\"recovery_mode\":";
  JsonEscape(out, RecoveryModeToString(report.base.recovery.recovery_mode));
  out << ",\"wal_replay_pending\":"
      << (report.base.recovery.wal_replay_pending ? "true" : "false")
      << ",\"unexpected_path_total\":"
      << report.base.recovery.unexpected_path_total << ",\"stable_lsn\":"
      << report.base.recovery.stable_lsn << ",\"inferred_path\":";
  JsonEscape(out, AttestInferredPathToString(report.base.inferred_path));
  out << ",\"compress\":{";
  out << "\"raw_total\":" << report.compress.raw_total << ",";
  out << "\"bytes_saved\":" << report.compress.bytes_saved << ",";
  out << "\"decompress_fail\":" << report.compress.decompress_fail;
  out << "},\"forbidden_violations\":[";
  for (size_t i = 0; i < report.forbidden_violations.size(); ++i) {
    if (i > 0) out << ",";
    JsonEscape(out, report.forbidden_violations[i]);
  }
  out << "],\"shards\":[";
  for (size_t i = 0; i < report.base.recovery.shards.size(); ++i) {
    if (i > 0) out << ",";
    const auto& s = report.base.recovery.shards[i];
    out << "{\"shard_id\":" << s.shard_id << ",\"state\":";
    JsonEscape(out, ShardRecoveryStateToString(s.state));
    out << ",\"inferred_path\":";
    JsonEscape(out, AttestInferredPathToString(s.inferred_path));
    out << ",\"read_tier_hits\":";
    WriteTierHits(out, s);
    out << "}";
  }
  out << "]}";
}

}  // namespace

std::string AttestExportV2ToJson(const AttestExportReportV2& report) {
  std::ostringstream oss;
  WriteKernelJson(oss, report);
  return oss.str();
}

std::string BuildChainBodyJson(uint64_t sequence, uint64_t checkpoint_lsn,
                               const std::string& prev_rar_sha256,
                               const std::string& op_log_head_sha256,
                               int64_t generated_at_unix,
                               const AttestExportReportV2& kernel) {
  std::ostringstream out;
  out << "{\"sequence\":" << sequence << ",\"checkpoint_lsn\":" << checkpoint_lsn
      << ",\"prev_rar_sha256\":";
  JsonEscape(out, prev_rar_sha256);
  out << ",\"op_log_head_sha256\":";
  JsonEscape(out, op_log_head_sha256);
  out << ",\"generated_at_unix\":" << generated_at_unix << ",\"kernel\":";
  WriteKernelJson(out, kernel);
  out << "}";
  return out.str();
}

}  // namespace audit
}  // namespace ebtree
