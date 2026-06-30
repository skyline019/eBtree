# ADR-026: RAR v2 Signing and Sidecars (Phase 3 / 3c)

## Status

Accepted — Ed25519 signing, pending_uncommitted WARN policy, and C API verify implemented (Phase 3b/3c).

## Context

Phase 2 introduced RAR v2 schema draft and `AttestExport`. Phase 3b added Ed25519 signing; Phase 3c hardens canonical JSON, CLI `--require-signature`, and C API `ebtree_audit_verify_signature`.

## Decision

### RAR v2 JSON (required fields)

- `rar_version`: `"2.0"`
- `attest_export_version`: `1`
- `catalog`: `{ path, table_count, key_set_source }` when sidecar present
- `op_log`: `{ path, entry_count, durable_entry_count, pending_count, key_set_source }`
- `signature`: base64 Ed25519 over canonical JSON body (optional on emit, required for signed deployments)

### Canonical JSON for signing (Phase 3c)

- UTF-8 body with `signature` field removed before hash
- Keys sorted lexicographically at each object level (stable stringify)
- No insignificant whitespace; numbers and booleans use JSON literals
- Golden vectors in `rar_canonical_sign_test` when `EBTREE_RAR_SIGNING=ON`

### Multishard recovery

- Per-shard `inferred_path` in `recovery.shard_state[]`
- Aggregate `recovery.inferred_path` = worst-case across shards (WalCorrupt > TLogFallback > LazyKey > …)

### kGroup op_log contract

- Durable set uses `durable_at_return: true` entries only
- `pending_uncommitted` populated for kGroup entries with `durable_at_return: false` — **implemented**; attestation emits WARN when pending remain after GroupCommit
- Attestation after `GroupCommit` must flip pending → durable in op_log sidecar

### Ed25519 signing (host-only)

- Keys never enter kernel
- CLI: `ebtree_audit sign`, `ebtree_audit verify --pubkey`, `--require-signature` (exit 1 if signature missing)
- C API: `ebtree_audit_verify_signature(json, sig_b64, pubkey_32)` (stub build: secret string as third arg)
- CMake option `EBTREE_RAR_SIGNING` (BCrypt on Windows, embedded ref10 elsewhere)
- Release CI: configure with `-DEBTREE_RAR_SIGNING=ON` and run `audit` suite (`RarCanonical`, `RarCApi`, `RarRequireSignature`)

### SQL parse / exec boundary (Phase 3d+)

- **Parse/exec** (`sql/parse/`, `sql/exec/`, `sql/eval/`) do not import `rar_builder` or attestation sidecars.
- **Session attestation** (`sql/session/attestation.cc`) is the only SQL-layer entry that calls `audit::BuildRar`.
- **Op-log hashing** in `sql/exec/executor.cc` uses `ebtree::Sha256HexString` from `ebtree/common/digest.h`, not `audit::`.
- Naming parallel: `RegistryParser` ↔ `BuildRar` entry; `parse_facade.h` ↔ RAR module layout (`rar_types`, builder, attestors).

### Single-pass open (P2)

- `BuildRarOptions.skip_physical_if_engine_open`: SQL attestation may skip offline physical when engine is reused

### Expect loader

- `ebtree_audit verify --expect expect.json` loads oracle key set (same shape as contract entries)

## Merge gates

- `ebtree_test_runner --suite=audit` ≥ 24
- `rar_schema_conformance_test`, `rar_canonical_sign_test`, `rar_capi_verify_test` green
- Sign/verify round-trip test

## References

- [ADR-022](022-recovery-attestation-report.md), [ADR-024](024-kernel-partial-unfreeze.md), [ADR-025](025-sql-oltp-complete-spec.md)
