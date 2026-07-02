#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ebtree_sql_db ebtree_sql_db;

enum {
  EBTREE_SQL_OK = 0,
  EBTREE_SQL_ERROR = -1,
  EBTREE_SQL_FEATURE_NOT_SUPPORTED = -2,
};

/** Product default for unset/zero-init attestation_mode: MONITOR (no sync Open BuildRar). */
enum ebtree_sql_attestation_mode {
  EBTREE_SQL_ATTEST_DEFAULT = 0,
  EBTREE_SQL_ATTEST_OFF = 1,
  EBTREE_SQL_ATTEST_REQUIRE_PASS = 2,
  EBTREE_SQL_ATTEST_ALLOW_WARN = 3,
};
#define EBTREE_SQL_ATTEST_MONITOR EBTREE_SQL_ATTEST_DEFAULT

typedef struct {
  int allows_write;
  uint64_t unexpected_path_total;
  uint64_t decompress_fail_total;
  uint64_t rar_chain_drop_total;
  uint64_t last_chain_sequence;
  uint64_t last_anchor_sequence;
  const char* last_anchor_hash;
  const char* last_chain_verdict;
  const char* last_chain_reason;
  int startup_chain_consistent;
  int worker_running;
} ebtree_sql_rar_status_t;

typedef struct {
  const char* path;
  const char* durability;
  uint64_t recovery_max_missing;
  int attestation_mode;
  const char* op_log_path;
} ebtree_sql_open_options_t;

typedef struct {
  const char* key;
  const char* value;
} ebtree_sql_row_t;

typedef struct {
  ebtree_sql_row_t* rows;
  size_t count;
} ebtree_sql_result_t;

int ebtree_sql_open(const ebtree_sql_open_options_t* opts,
                    ebtree_sql_db** out);
int ebtree_sql_execute(ebtree_sql_db* db, const char* sql,
                       ebtree_sql_result_t* result);
void ebtree_sql_result_free(ebtree_sql_result_t* result);
void ebtree_sql_close(ebtree_sql_db* db);
const char* ebtree_sql_last_error(ebtree_sql_db* db);
int ebtree_sql_rar_status(ebtree_sql_db* db, ebtree_sql_rar_status_t* out);

#ifdef __cplusplus
}
#endif
