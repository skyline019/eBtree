#include "ebtree_sql.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "ebtree_sql.h"
#include "sql/session/database.h"

using ebtree::audit::RarStatusSnapshot;

using ebtree::DurabilityClass;
using ebtree::sql::AttestationMode;
using ebtree::sql::Database;
using ebtree::sql::ExecuteResult;
using ebtree::sql::OpenOptions;

namespace {

struct EbtreeSqlDb {
  std::unique_ptr<Database> db;
};

char* DupCStr(const char* s) {
  if (!s) return nullptr;
  const size_t n = std::strlen(s);
  char* out = static_cast<char*>(std::malloc(n + 1));
  if (!out) return nullptr;
  std::memcpy(out, s, n + 1);
  return out;
}

DurabilityClass ParseDurability(const char* durability) {
  if (!durability) return DurabilityClass::kBalanced;
  const std::string s(durability);
  if (s == "sync") return DurabilityClass::kSync;
  if (s == "group") return DurabilityClass::kGroup;
  return DurabilityClass::kBalanced;
}

AttestationMode ParseAttestation(int mode) {
  switch (mode) {
    case EBTREE_SQL_ATTEST_OFF:
      return AttestationMode::kOff;
    case EBTREE_SQL_ATTEST_REQUIRE_PASS:
      return AttestationMode::kRequirePass;
    case EBTREE_SQL_ATTEST_ALLOW_WARN:
      return AttestationMode::kAllowWarn;
    case EBTREE_SQL_ATTEST_DEFAULT:
      return AttestationMode::kMonitor;
    default:
      return AttestationMode::kMonitor;
  }
}

OpenOptions ToOpenOptions(const ebtree_sql_open_options_t* opts) {
  OpenOptions out{};
  out.path = opts->path ? opts->path : "";
  out.durability = ParseDurability(opts->durability);
  out.recovery_max_missing = opts->recovery_max_missing;
  out.attestation = ParseAttestation(opts->attestation_mode);
  if (opts->op_log_path) out.op_log_path = opts->op_log_path;
  return out;
}

int CopyResult(const ExecuteResult& src, ebtree_sql_result_t* dst) {
  if (!dst) return 0;
  dst->count = src.rows.size();
  if (dst->count == 0) {
    dst->rows = nullptr;
    return 0;
  }
  dst->rows = new (std::nothrow) ebtree_sql_row_t[dst->count];
  if (!dst->rows) return -1;
  for (size_t i = 0; i < dst->count; ++i) {
    dst->rows[i].key = DupCStr(src.rows[i].key.c_str());
    dst->rows[i].value = DupCStr(src.rows[i].value.c_str());
    if (!dst->rows[i].key || !dst->rows[i].value) return -1;
  }
  return 0;
}

}  // namespace

extern "C" int ebtree_sql_open(const ebtree_sql_open_options_t* opts,
                               ebtree_sql_db** out) {
  if (!opts || !opts->path || !out) return -1;
  std::unique_ptr<Database> db;
  const ebtree::Status st = Database::Open(ToOpenOptions(opts), &db);
  if (!st.ok()) return -1;
  auto* handle = new (std::nothrow) EbtreeSqlDb{};
  if (!handle) return -1;
  handle->db = std::move(db);
  *out = reinterpret_cast<ebtree_sql_db*>(handle);
  return 0;
}

extern "C" int ebtree_sql_execute(ebtree_sql_db* db, const char* sql,
                                  ebtree_sql_result_t* result) {
  if (!db || !sql) return EBTREE_SQL_ERROR;
  auto* handle = reinterpret_cast<EbtreeSqlDb*>(db);
  ExecuteResult exec_result{};
  const ebtree::Status st = handle->db->ExecuteSql(sql, &exec_result);
  if (!st.ok()) {
    if (st.message().find("SQLFeatureNotSupported") != std::string::npos) {
      return EBTREE_SQL_FEATURE_NOT_SUPPORTED;
    }
    return EBTREE_SQL_ERROR;
  }
  if (result && CopyResult(exec_result, result) != 0) return EBTREE_SQL_ERROR;
  return EBTREE_SQL_OK;
}

extern "C" void ebtree_sql_result_free(ebtree_sql_result_t* result) {
  if (!result) return;
  for (size_t i = 0; i < result->count; ++i) {
    std::free(const_cast<char*>(result->rows[i].key));
    std::free(const_cast<char*>(result->rows[i].value));
  }
  delete[] result->rows;
  result->rows = nullptr;
  result->count = 0;
}

extern "C" void ebtree_sql_close(ebtree_sql_db* db) {
  if (!db) return;
  auto* handle = reinterpret_cast<EbtreeSqlDb*>(db);
  handle->db->Close();
  delete handle;
}

extern "C" const char* ebtree_sql_last_error(ebtree_sql_db* db) {
  if (!db) return "";
  auto* handle = reinterpret_cast<EbtreeSqlDb*>(db);
  return handle->db->last_error().c_str();
}

namespace {

struct RarStatusStrings {
  std::string verdict;
  std::string reason;
  std::string anchor_hash;
};

}  // namespace

extern "C" int ebtree_sql_rar_status(ebtree_sql_db* db,
                                     ebtree_sql_rar_status_t* out) {
  if (!db || !out) return EBTREE_SQL_ERROR;
  auto* handle = reinterpret_cast<EbtreeSqlDb*>(db);
  const RarStatusSnapshot snap = handle->db->rar_monitor().StatusSnapshot();
  static thread_local RarStatusStrings cached;
  cached.verdict = snap.last_chain_verdict;
  cached.reason = snap.last_chain_reason;
  cached.anchor_hash = snap.last_anchor_hash;
  out->allows_write = snap.allows_write ? 1 : 0;
  out->unexpected_path_total = snap.unexpected_path_total;
  out->decompress_fail_total = snap.decompress_fail_total;
  out->rar_chain_drop_total = snap.rar_chain_drop_total;
  out->last_chain_sequence = snap.last_chain_sequence;
  out->last_anchor_sequence = snap.last_anchor_sequence;
  out->last_anchor_hash = cached.anchor_hash.c_str();
  out->last_chain_verdict = cached.verdict.c_str();
  out->last_chain_reason = cached.reason.c_str();
  out->startup_chain_consistent = snap.startup_chain_consistent ? 1 : 0;
  out->worker_running = snap.worker_running ? 1 : 0;
  return EBTREE_SQL_OK;
}
