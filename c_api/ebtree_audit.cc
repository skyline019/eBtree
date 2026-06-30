#include "ebtree_audit.h"

#include <cstring>
#include <string>

#include "json_writer.h"
#include "op_log_expect.h"
#include "catalog_expect.h"
#include "rar_builder.h"
#include "rar_types.h"
#include "rar_sign.h"

using ebtree::audit::BuildRarOptions;
using ebtree::audit::ContractMode;
using ebtree::audit::DurabilityClassFromString;
using ebtree::audit::ExpectSnapshot;
using ebtree::audit::LoadCatalogExpectSnapshot;
using ebtree::audit::LoadOpLogExpectSnapshot;
using ebtree::audit::RarReport;
using ebtree::audit::RarReportToJson;
using ebtree::audit::RarVerdict;

namespace {

int CopyOut(const std::string& json, char* out_json, size_t out_cap) {
  if (!out_json || out_cap == 0) return EBTREE_AUDIT_INVALID;
  if (json.size() + 1 > out_cap) return EBTREE_AUDIT_INVALID;
  std::memcpy(out_json, json.data(), json.size());
  out_json[json.size()] = '\0';
  return 0;
}

RarVerdict ParseVerdictString(const std::string& s) {
  if (s == "PASS") return RarVerdict::kPass;
  if (s == "WARN") return RarVerdict::kWarn;
  if (s == "REFUSE_START") return RarVerdict::kRefuseStart;
  return RarVerdict::kRefuseStart;
}

int VerdictToC(RarVerdict v) {
  switch (v) {
    case RarVerdict::kPass:
      return EBTREE_AUDIT_PASS;
    case RarVerdict::kWarn:
      return EBTREE_AUDIT_WARN;
    case RarVerdict::kRefuseStart:
      return EBTREE_AUDIT_REFUSE_START;
  }
  return EBTREE_AUDIT_REFUSE_START;
}

}  // namespace

extern "C" int ebtree_audit_attest(const char* path, const char* tier,
                                   char* out_json, size_t out_cap) {
  if (!path) return EBTREE_AUDIT_INVALID;

  BuildRarOptions opts{};
  opts.engine_path = path;
  opts.durability_tier =
      DurabilityClassFromString(tier ? tier : "balanced");

  RarReport report{};
  const ebtree::Status st = ebtree::audit::BuildRar(opts, &report);
  if (!st.ok()) return EBTREE_AUDIT_INVALID;

  const std::string json = RarReportToJson(report);
  if (CopyOut(json, out_json, out_cap) != 0) return EBTREE_AUDIT_INVALID;
  return VerdictToC(report.verdict);
}

extern "C" int ebtree_audit_verdict_from_json(const char* rar_json) {
  if (!rar_json) return EBTREE_AUDIT_INVALID;
  const std::string s(rar_json);
  const auto pos = s.find("\"verdict\"");
  if (pos == std::string::npos) return EBTREE_AUDIT_INVALID;
  const auto q1 = s.find('"', pos + 9);
  if (q1 == std::string::npos) return EBTREE_AUDIT_INVALID;
  const auto q2 = s.find('"', q1 + 1);
  if (q2 == std::string::npos) return EBTREE_AUDIT_INVALID;
  const std::string verdict = s.substr(q1 + 1, q2 - q1 - 1);
  return VerdictToC(ParseVerdictString(verdict));
}

extern "C" int ebtree_audit_verify_with_sidecars(const char* path,
                                                 const char* tier,
                                                 const char* op_log_path,
                                                 const char* catalog_path,
                                                 char* out_json,
                                                 size_t out_cap) {
  if (!path) return EBTREE_AUDIT_INVALID;

  BuildRarOptions opts{};
  opts.engine_path = path;
  opts.durability_tier =
      DurabilityClassFromString(tier ? tier : "balanced");
  if (op_log_path) opts.op_log_path = op_log_path;
  if (catalog_path) opts.catalog_path = catalog_path;

  ExpectSnapshot expect{};
  if (op_log_path) {
    if (!LoadOpLogExpectSnapshot(op_log_path, ContractMode::kDurable, &expect)
             .ok()) {
      return EBTREE_AUDIT_INVALID;
    }
    opts.expect = &expect;
  }
  if (catalog_path) {
    if (!LoadCatalogExpectSnapshot(catalog_path, &expect).ok()) {
      return EBTREE_AUDIT_INVALID;
    }
    opts.expect = &expect;
  }

  RarReport report{};
  const ebtree::Status st = ebtree::audit::BuildRar(opts, &report);
  if (!st.ok()) return EBTREE_AUDIT_INVALID;

  const std::string json = RarReportToJson(report);
  if (CopyOut(json, out_json, out_cap) != 0) return EBTREE_AUDIT_INVALID;
  return VerdictToC(report.verdict);
}

extern "C" int ebtree_audit_verify_signature(const char* json_body,
                                             const char* sig_b64,
                                             const char* pubkey_or_secret) {
  if (!json_body || !sig_b64 || !pubkey_or_secret) return EBTREE_AUDIT_INVALID;
#if defined(EBTREE_RAR_SIGNING)
  const std::string pubkey(pubkey_or_secret, 32);
#else
  const std::string pubkey(pubkey_or_secret);
#endif
  const ebtree::Status st =
      ebtree::audit::VerifyRarSignature(json_body, sig_b64, pubkey);
  return st.ok() ? 0 : EBTREE_AUDIT_REFUSE_START;
}
