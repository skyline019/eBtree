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
  EBTREE_SQL_ATTEST_OFF = 0,
  EBTREE_SQL_ATTEST_REQUIRE_PASS = 1,
  EBTREE_SQL_ATTEST_ALLOW_WARN = 2,
};

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

#ifdef __cplusplus
}
#endif
