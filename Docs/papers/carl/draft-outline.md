# CARL: Perf-Neutral Recovery Attestation for Embedded OLTP Engines

**Target**: EuroSys / VLDB (embedded + verifiable narrative)  
**Status**: Outline (M7–M8 full draft)

## Abstract (draft)

Embedded OLTP engines rely on checkpoint recovery but rarely expose tamper-evident attestation without sync overhead. We present CARL (Checkpoint-Attestation Recovery Log), an abstraction that chains checkpoint digests asynchronously, batches Merkle roots, and publishes external signed tree heads — while preserving ≥0.99× write throughput under MONITOR mode.

## 1. Introduction

- Problem: local admin can rewrite recovery logs
- Contribution: CARL abstraction + anchor + Merkle proofs + artifact

## 2. Background

- ARIES recovery, RAR product baseline (ADR-038/040)
- Threat model (admin / crash / bit-flip)

## 3. Design

- Async chain worker (post-checkpoint)
- Merkle batch accumulator (RFC 6962-style domains)
- External STH anchor (`EBTREE_CARL_ANCHOR_PATH`)
- MONITOR vs REQUIRE_PASS SKU boundary

## 4. Implementation

- eB-Tree kernel integration (no hot-path blocking)
- CLI: `chain-verify --require-anchor`, `chain-proof`

## 5. Evaluation

- YCSB 100k write + verify latency
- Tamper injection (CarlAnchorTamperDetect)
- Perf gate: KBalancedWrite100kWithRarMonitor ≥0.99×

## 6. Related Work

See [related-work.md](related-work.md)

## 7. Conclusion

## Artifact

- `ebtree_audit chain-verify --require-anchor`
- `demo/run_scenario.ps1`
- Gate `P15-carl-complete` logs
