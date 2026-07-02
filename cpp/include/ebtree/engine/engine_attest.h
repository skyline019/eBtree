#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ebtree/common/config.h"
#include "ebtree/concept/codec/codec_registry.h"
#include "ebtree/concept/recovery/recovery_state.h"
#include "ebtree/engine/read_tier.h"

namespace ebtree {

class Engine;

enum class AttestInferredPath {
  kFastOpenDeferred,
  kWalReplayComplete,
  kTLogFallback,
  kLazyKey,
  kOnDiskLazy,
  kCommittedHot,
  kUnknown,
};

struct RecoveryShardSnapshot {
  uint32_t shard_id{0};
  ShardRecoveryState state{ShardRecoveryState::kCommittedHot};
  AttestInferredPath inferred_path{AttestInferredPath::kUnknown};
  bool wal_corrupt{false};
  bool lazy_root_corrupt{false};
  uint64_t read_tier_hits[kReadTierCount]{};
};

struct RecoverySnapshot {
  RecoveryMode recovery_mode{RecoveryMode::kHot};
  bool wal_replay_pending{false};
  uint64_t unexpected_path_total{0};
  uint64_t stable_lsn{0};
  std::vector<RecoveryShardSnapshot> shards;
};

struct AttestKeyProbe {
  uint32_t shard{0};
  std::string key;
  bool found{false};
  std::string value_sha256;
  ReadTier read_tier{ReadTier::kCount};
};

struct AttestExportOptions {
  bool any_badwal{false};
  std::vector<std::string> probe_keys;
};

struct AttestExportReport {
  RecoverySnapshot recovery{};
  AttestInferredPath inferred_path{AttestInferredPath::kUnknown};
  std::vector<AttestKeyProbe> probes;
};

struct AttestExportReportV2 {
  AttestExportReport base{};
  uint64_t checkpoint_lsn{0};
  uint64_t pages_touched{0};
  CompressStatsSnapshot compress{};
  std::vector<std::string> forbidden_violations;
};

using GroupCommitObserver = std::function<void(Engine* engine)>;
using CheckpointObserver = std::function<void(Engine* engine, uint64_t checkpoint_lsn)>;
using WriteGuard = std::function<Status()>;

std::string ReadTierToString(ReadTier tier);
std::string ShardRecoveryStateToString(ShardRecoveryState state);
std::string AttestInferredPathToString(AttestInferredPath path);
std::string RecoveryModeToString(RecoveryMode mode);

Status AttestExport(Engine* engine, const AttestExportOptions& opts,
                    AttestExportReport* out);
Status AttestExportV2(Engine* engine, const AttestExportOptions& opts,
                      AttestExportReportV2* out);
Status AttestExportSnapshot(Engine* engine, uint64_t checkpoint_lsn,
                            AttestExportReportV2* out);

}  // namespace ebtree
