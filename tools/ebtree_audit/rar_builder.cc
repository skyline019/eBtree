#include "rar_builder.h"

#include <chrono>
#include <filesystem>
#include <memory>

#include "contract_attestor.h"
#include "physical_attestor.h"
#include "policy_gate.h"
#include "rar_chain.h"
#include "recovery_attestor.h"
#include "shard_paths.h"
#include "sidecar_stats.h"
#include "tier_consistency_attestor.h"

#include "ebtree/engine/engine_attest.h"

namespace ebtree {
namespace audit {

namespace {

EngineOptions MakeEngineOptions(const BuildRarOptions& opts) {
  if (!opts.engine_options.path.empty()) return opts.engine_options;
  EngineOptions engine_opts =
      EngineOptions::StandardDefaults(opts.engine_path);
  engine_opts.path = opts.engine_path;
  engine_opts.durability = opts.durability_tier;
  if (opts.shard_count > 0) {
    engine_opts.shard_count = opts.shard_count;
  }
  return engine_opts;
}

bool AnyBadWal(const PhysicalReport& physical) {
  for (const auto& shard : physical.shards) {
    if (shard.wal.badwal_marker) return true;
  }
  return false;
}

std::vector<std::string> CollectProbeKeys(const BuildRarOptions& opts) {
  if (!opts.probe_keys.empty()) return opts.probe_keys;
  if (!opts.expect) return {};
  std::vector<std::string> keys;
  keys.reserve(opts.expect->touched_keys.size());
  for (const auto& key : opts.expect->touched_keys) {
    keys.push_back(key);
  }
  return keys;
}

}  // namespace

Status BuildPhysicalOnly(const std::string& engine_path, RarReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->rar_version = "3.0";
  out->engine_path = engine_path;
  out->shard_count = DiscoverShardCount(engine_path);
  return PhysicalAttest(engine_path, out->shard_count, &out->physical);
}

Status BuildRar(const BuildRarOptions& opts, RarReport* out,
                std::unique_ptr<Engine>* engine_out) {
  if (!out) return Status::InvalidArgument("out is null");

  out->rar_version = "3.0";
  out->generated_at_unix =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  out->engine_path = opts.engine_path;
  out->durability_tier = opts.durability_tier;
  out->policy = opts.policy;

  out->shard_count =
      opts.shard_count > 0 ? opts.shard_count : DiscoverShardCount(opts.engine_path);

  if (!opts.skip_physical_if_engine_open || !engine_out) {
    const Status ps =
        PhysicalAttest(opts.engine_path, out->shard_count, &out->physical);
    if (!ps.ok()) return ps;
  }

  bool any_badwal = AnyBadWal(out->physical);
  if (opts.skip_physical_if_engine_open && engine_out && out->physical.shards.empty()) {
    PhysicalReport probe{};
    if (PhysicalAttest(opts.engine_path, out->shard_count, &probe).ok()) {
      out->physical = probe;
      any_badwal = AnyBadWal(probe);
    }
  }

  if (!opts.op_log_path.empty()) {
    (void)CollectOpLogSidecarStats(opts.op_log_path, &out->op_log);
  }
  if (!opts.catalog_path.empty()) {
    (void)CollectCatalogSidecarStats(opts.catalog_path, &out->catalog);
  }

  if (!opts.include_recovery) {
    out->verdict = RarVerdict::kPass;
    return Status::Ok();
  }

  const EngineOptions engine_opts = MakeEngineOptions(opts);
  std::unique_ptr<Engine> engine;
  const Status os = Engine::Open(engine_opts, &engine);
  if (!os.ok()) {
    out->verdict = RarVerdict::kRefuseStart;
    out->verdict_reason = "Engine::Open failed: " + os.message();
    return Status::Ok();
  }

  const std::vector<std::string> keys = CollectProbeKeys(opts);
  const Status rs = RecoveryAttest(engine.get(), keys, any_badwal, &out->recovery);
  if (!rs.ok()) return rs;

  if (opts.expect) {
    const Status cs = ContractAttest(*opts.expect, out->recovery, &out->contract);
    if (!cs.ok()) return cs;
  }

  AttestExportReportV2 kernel_v2{};
  AttestExportOptions v2_opts{};
  v2_opts.any_badwal = any_badwal;
  v2_opts.probe_keys = keys;
  if (AttestExportV2(engine.get(), v2_opts, &kernel_v2).ok()) {
    out->kernel.checkpoint_lsn = kernel_v2.checkpoint_lsn;
    out->kernel.pages_touched = kernel_v2.pages_touched;
    out->kernel.unexpected_path_total =
        kernel_v2.base.recovery.unexpected_path_total;
    out->kernel.stable_lsn = kernel_v2.base.recovery.stable_lsn;
    out->kernel.recovery_mode =
        RecoveryModeToString(kernel_v2.base.recovery.recovery_mode);
    out->kernel.inferred_path =
        AttestInferredPathToString(kernel_v2.base.inferred_path);
    out->kernel.compress_raw_total = kernel_v2.compress.raw_total;
    out->kernel.compress_bytes_saved = kernel_v2.compress.bytes_saved;
    out->kernel.decompress_fail = kernel_v2.compress.decompress_fail;
    out->kernel.forbidden_violations = kernel_v2.forbidden_violations;
  }

  const TierConsistencyReport tier = CheckTierConsistency(*out);
  out->tier_contract.consistent = tier.consistent;
  out->tier_contract.issues = tier.issues;

  const std::string chain_path =
      (std::filesystem::path(opts.engine_path) / "ebtree.rar.chain.jsonl")
          .string();
  std::vector<RarChainEntry> chain_entries;
  if (ReadRarChainEntries(chain_path, &chain_entries).ok() &&
      !chain_entries.empty()) {
    const auto& last = chain_entries.back();
    out->sidecar_chain.sequence = last.sequence;
    out->sidecar_chain.prev_rar_sha256 = last.prev_rar_sha256;
    out->sidecar_chain.rar_sha256 = last.rar_sha256;
    out->sidecar_chain.op_log_head_sha256 = last.op_log_head_sha256;
  }

  out->verdict = ApplyPolicyGate(*out, &out->verdict_reason);

  if (engine_out) {
    *engine_out = std::move(engine);
  }

  return Status::Ok();
}

}  // namespace audit
}  // namespace ebtree
