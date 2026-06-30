#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum EbtreeAuditVerdict {
  EBTREE_AUDIT_PASS = 0,
  EBTREE_AUDIT_REFUSE_START = 1,
  EBTREE_AUDIT_WARN = 2,
  EBTREE_AUDIT_INVALID = 3,
};

int ebtree_audit_attest(const char* path, const char* tier, char* out_json,
                        size_t out_cap);

int ebtree_audit_verdict_from_json(const char* rar_json);

int ebtree_audit_verify_with_sidecars(const char* path, const char* tier,
                                      const char* op_log_path,
                                      const char* catalog_path,
                                      char* out_json, size_t out_cap);

/** Returns 0 if signature valid, non-zero otherwise. pubkey is 32 raw bytes. */
int ebtree_audit_verify_signature(const char* json_body, const char* sig_b64,
                                  const char* pubkey_32);

#ifdef __cplusplus
}
#endif
