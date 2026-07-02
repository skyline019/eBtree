# ADR-040: RAR Standard SKU Defaults

## Status

Accepted (2026-07-01)

## Context

ADR-038 defines async RAR chain + `AttestationMode::kMonitor` soft write circuit. Prior to this ADR, attestation defaulted to `kOff`; KV-only `Engine::Open` users had no product wrapper for monitor install.

Product goal: **Standard SKU** ships with RAR monitor + async chain **on by default**, with **zero synchronous `BuildRar` on Open** (perf-neutral) and opt-in L2 (`REQUIRE_PASS` / `ALLOW_WARN`) for compliance upgrades.

## Decision

### L1 default: MONITOR + async chain

| Surface | Default |
|---------|---------|
| `OpenOptions::attestation` | `AttestationMode::kMonitor` |
| `OpenStmt::attestation` (SQL OPEN) | `kMonitor` |
| C API `attestation_mode` unset / `0` | `kMonitor` (`EBTREE_SQL_ATTEST_DEFAULT`) |
| `attestation_async` | `true` (unchanged) |

Open for `kOff` / `kMonitor` **does not** run synchronous `RunOpenAttestation` / `BuildRar`. `REQUIRE_PASS` and `ALLOW_WARN` retain sync open attestation.

### KV product entry

`audit::OpenWithRarMonitor` (`tools/ebtree_audit/rar_monitor.h`) wraps `Engine::Open` + `InstallRarMonitor`. `cpp/include/ebtree/engine/engine_rar.h` re-exports the audit header for documentation; link `ebtree_audit_lib`.

### MONITOR write circuit (dual-track)

`RarMonitor::RefreshRuntimeState` fuses:

1. **Engine stats**: `unexpected_path_total`, `decompress_fail_total` vs `RarPolicy`
2. **Chain policy**: last worker `EvaluateSnapshotPolicy` verdict cached via `on_snapshot` callback

Default: chain verdict `PASS` until first snapshot. Violations open the write circuit; reads remain available.

### Observability

- `PRAGMA rar_status` — key/value rows (`allows_write`, stats, chain verdict, startup verify)
- C API `ebtree_sql_rar_status()` — same snapshot struct

### Signing (optional, non-blocking)

When `EBTREE_RAR_KEY` is set, `RarChainWorker` signs **chain `body_json`** (canonical strip-signature, Ed25519 when `EBTREE_RAR_SIGNING` at build). No key → one-time stderr WARN; checkpoint/Open unaffected.

Chain body signing is **separate** from Open RAR report canonical signing (ADR-022).

### Chain ops

- `RotateRarChainIfNeeded` at 10k entries (worker post-append check)
- Async `VerifyRarChain` after `Install` (non-blocking Open); exposed as `startup_chain_consistent` in status

### Perf gate

`EbPipelinePerf.KBalancedWrite100kWithRarMonitor` (Release): monitor + chain ≥ 0.99× raw `ProductionDefaults` baseline.

### Opt-in L2

`REQUIRE_PASS` / `ALLOW_WARN` unchanged; tests that seed data without monitor should set `attestation = kOff` explicitly.

## Consequences

- Tests assuming implicit `kOff` must set it explicitly.
- SQL layer depends on `ebtree_audit_lib` for `RarMonitor`.
- `P14-rar-product` gate covers monitor smoke, chain roundtrip, and write perf.

## References

- ADR-038: RAR–Kernel Full Auditability
- `Docs/archive/rar/rar-product-implementation-2026-07-01.md`
