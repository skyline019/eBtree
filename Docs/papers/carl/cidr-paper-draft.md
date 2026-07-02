# CIDR 2027 Paper Draft (≤6 pages)

**Title**: CARL: Perf-Neutral Recovery Attestation for Embedded OLTP Engines

## Abstract

Embedded OLTP engines checkpoint recovery state locally but rarely expose tamper-evident attestation without blocking writes. We present CARL (Checkpoint-Attestation Recovery Log): an async hash chain over post-commit kernel exports, Merkle batch roots, and optional external Signed Tree Heads (STH). Under MONITOR mode, CARL preserves ≥0.99× write throughput while enabling `chain-verify --require-anchor` after crash.

## 1. Introduction

- Problem: local admin can rewrite recovery logs
- Contributions: CARL abstraction; external STH; experimental eval on eB-Tree

## 2. Background and Threat Model

- ARIES recovery vs attestation (ADR-038/041)
- Adversaries: crash, buggy kernel, local admin rewrite, bit-flip
- Tamper-evident with optional anchored verification (not tamper-proof alone)

## 3. Design

- Async worker on checkpoint observer (no hot-path IO)
- Hash chain + optional Ed25519 signatures
- Merkle batch sidecar (`.merkle.jsonl`) + inclusion proofs
- External anchor (`EBTREE_CARL_ANCHOR_PATH`, `.sth.jsonl`)
- MONITOR vs REQUIRE_PASS SKU boundary

## 4. Implementation

- eB-Tree integration: `OpenWithRarMonitor`, `PRAGMA rar_status`
- CLI: `chain-anchor`, `chain-verify --require-anchor`, `chain-proof`

## 5. Evaluation

See [eval-results.md](eval-results.md):

- 100k write TPS: no-CARL vs CARL MONITOR (ratio ≥ 0.99)
- Chain verify latency (1k entries)
- Anchor publish latency
- Tamper injection (`CarlAnchorTamperDetect`)

## 6. Related Work

GlassDB, QLDB, PoWER, CT RFC6962 — see [related-work.md](related-work.md).

## 7. Conclusion

CARL makes recovery attestation a first-class, perf-neutral embedded primitive.

## Artifact

`P15-carl-complete` + `P16-carl-eval` + `demo/run_scenario.ps1 -Scenario industrial`
