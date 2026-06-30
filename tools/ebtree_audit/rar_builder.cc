#include "rar_builder.h"

#include <chrono>
#include <memory>

#include "contract_attestor.h"
#include "physical_attestor.h"
#include "policy_gate.h"
#include "recovery_attestor.h"
#include "shard_paths.h"
#include "sidecar_stats.h"

namespace ebtree {
namespace audit {

namespace {

EngineOptions MakeEngineOptions(const BuildRarOptions& opts) {
  if (!opts.engine_options.path.empty()) return opts.engine_options;
  EngineOptions engine_opts =
      EngineOptions::ProductionDefaults(opts.engine_path);
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
  out->rar_version = "2.0";
  out->engine_path = engine_path;
  out->shard_count = DiscoverShardCount(engine_path);
  return PhysicalAttest(engine_path, out->shard_count, &out->physical);
}

Status BuildRar(const BuildRarOptions& opts, RarReport* out,
                std::unique_ptr<Engine>* engine_out) {
  if (!out) return Status::InvalidArgument("out is null");

  out->rar_version = "2.0";
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

  out->verdict = ApplyPolicyGate(*out, &out->verdict_reason);

  if (engine_out) {
    *engine_out = std::move(engine);
  }

  return Status::Ok();
}

}  // namespace audit
}  // namespace ebtree
