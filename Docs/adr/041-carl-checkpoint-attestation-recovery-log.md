# ADR-041: CARL — Checkpoint-Attestation Recovery Log

## Status

Accepted (2026-07-01)

## Context

ADR-040 productized RAR as Standard SKU default (MONITOR + async chain). Academic and market feedback converge on two gaps:

1. **Trust anchor**: local hash chain alone is tamper-*evident* only when verified against an external Signed Tree Head (STH).
2. **Proof semantics**: linear chain verify is O(n); batch Merkle roots enable inclusion proofs and CT-style anchoring.

CARL names and formalizes the abstraction already implemented as `ebtree.rar.chain.jsonl` + worker + monitor.

## Decision

### CARL abstraction

**CARL** (Checkpoint-Attestation Recovery Log) is an append-only log where each entry is bound to a **post-commit checkpoint** and contains:

- `AttestExportSnapshot` kernel state (no probe)
- `prev_rar_sha256` hash chain over canonical `body_json`
- optional Ed25519 signature (`EBTREE_RAR_KEY`)
- optional `merkle_batch_root` at batch boundaries

Hot path: `CheckpointObserver` → enqueue only (ADR-038/040). Durability never depends on CARL IO.

### Threat model

| Adversary | Capability | CARL response |
|-----------|------------|---------------|
| **Crash** | power fail mid-write | post-commit observer; chain drop ≠ rollback |
| **Buggy kernel** | forbidden read path | stats → MONITOR write circuit |
| **Local admin** | rewrite chain file | detect via `--require-anchor` vs external STH |
| **Bit-flip** | silent corruption | decompress_fail / policy REFUSE |

CARL does **not** claim tamper-*proof* without external anchor. Document as **tamper-evident with optional anchored verification**.

### External trust anchor (STH)

- Publish: `ebtree_audit chain-anchor --path DIR [--anchor-dir PATH]`
- STH file: `{chain_basename}.sth.jsonl` — `sequence`, `root_hash`, `published_at_unix`, optional `signature`
- Env: `EBTREE_CARL_ANCHOR_PATH` (default: `{engine_path}/carl_anchors/`)

Worker may auto-publish on rotate when anchor path set.

### Related work positioning

| System | Setting | CARL difference |
|--------|---------|-----------------|
| **GlassDB** (VLDB'23) | untrusted cloud DB + TX ledger | embedded engine; attestation = recovery state not document |
| **QLDB** | managed journal + Merkle proof API | no server; checkpoint-bound kernel export |
| **PoWER/CapybaraKV** (OSDI'25) | verified PM KV crash consistency | empirical CARL + runtime fail-stop, not Coq proof |
| **Certificate Transparency** | log operator + STH | CARL STH anchors checkpoint chain tail |
| **ARIES** | redo/undo correctness | CARL answers "prove recovery state at checkpoint K" |

### Observability extensions

`PRAGMA rar_status` / `ebtree_sql_rar_status` add:

- `last_anchor_sequence`, `last_anchor_hash`

## Consequences

- New modules: `rar_chain_anchor`, `rar_merkle`
- CLI: `chain-anchor`, `chain-verify --require-anchor`, `chain-proof`
- Gate: `P15-carl-complete`
- ADR-040 remains product default spec; ADR-041 is research/market abstraction layer

## References

- ADR-038, ADR-040
- Crosby & Wallach, USENIX Security 2009 (history tree)
- RFC 6962 (Certificate Transparency)
- GlassDB, VLDB 2023; PoWER, OSDI 2025
