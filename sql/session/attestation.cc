#include "attestation.h"

#include <filesystem>

#include "catalog_expect.h"
#include "op_log_expect.h"
#include "rar_builder.h"

namespace ebtree {
namespace sql {

namespace {

AttestationVerdict MapVerdict(audit::RarVerdict v) {
  switch (v) {
    case audit::RarVerdict::kPass:
      return AttestationVerdict::kPass;
    case audit::RarVerdict::kWarn:
      return AttestationVerdict::kWarn;
    case audit::RarVerdict::kRefuseStart:
      return AttestationVerdict::kRefuseStart;
  }
  return AttestationVerdict::kRefuseStart;
}

bool AnyBadWal(const audit::RarReport& report) {
  for (const auto& shard : report.physical.shards) {
    if (shard.wal.badwal_marker) return true;
  }
  return false;
}

}  // namespace

bool AttestationAllowsOpen(AttestationMode mode, AttestationVerdict verdict) {
  switch (mode) {
    case AttestationMode::kOff:
      return true;
    case AttestationMode::kRequirePass:
      return verdict == AttestationVerdict::kPass;
    case AttestationMode::kAllowWarn:
      return verdict == AttestationVerdict::kPass ||
             verdict == AttestationVerdict::kWarn;
  }
  return false;
}

Status RunOpenAttestation(const OpenOptions& opts, AttestationReport* report,
                          std::unique_ptr<Engine>* engine_out) {
  if (!report) return Status::InvalidArgument("report is null");

  audit::BuildRarOptions rar_opts{};
  rar_opts.engine_path = opts.path;
  rar_opts.durability_tier = opts.durability;
  rar_opts.engine_options = opts.ToEngineOptions();
  rar_opts.policy.recovery_max_missing = opts.recovery_max_missing;

  audit::ExpectSnapshot expect{};
  const std::string op_log_path = opts.DefaultOpLogPath();
  if (std::filesystem::exists(op_log_path)) {
    const Status ls = audit::LoadOpLogExpectSnapshot(
        op_log_path, audit::ContractMode::kDurable, &expect);
    if (!ls.ok()) return ls;
    rar_opts.expect = &expect;
  }

  const std::string catalog_path = opts.DefaultCatalogPath();
  if (std::filesystem::exists(catalog_path)) {
    const Status cs = audit::LoadCatalogExpectSnapshot(catalog_path, &expect);
    if (!cs.ok()) return cs;
    rar_opts.expect = &expect;
  }

  rar_opts.skip_physical_if_engine_open = engine_out != nullptr;
  rar_opts.op_log_path = op_log_path;
  rar_opts.catalog_path = catalog_path;

  audit::RarReport rar{};
  const Status st = audit::BuildRar(rar_opts, &rar, engine_out);
  if (!st.ok()) return st;

  report->verdict = MapVerdict(rar.verdict);
  report->verdict_reason = rar.verdict_reason;
  report->any_badwal = AnyBadWal(rar);
  return Status::Ok();
}

}  // namespace sql
}  // namespace ebtree
