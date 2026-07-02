# ADR-042: Phase 2 Evidence Sprint

> **Superseded for active roadmap by [ADR-043](043-kernel-rar-deep-cultivation.md)** (2026-07-01). Content retained for history.

## Status

Accepted (2026-07-01)

## Context

Phase 1 delivered CARL engineering closure (P15). Phase 2 shifts ROI to **evidence chain**: market e2e demos, academic eval table, CIDR 2027 sprint, ADR promise completion (anchor signature verify, worker auto-publish).

## Decision

### Market

- Real demo executables: `ebtree_demo_industrial`, `ebtree_demo_medical`, `ebtree_demo_finance`
- Shared library `ebtree_demo_flows`; gate `P16-demo-e2e`

### Academic

- `ebtree_carl_eval` bench + `eval-results.md`
- Gate `P16-carl-eval` (no-CARL vs CARL ratio ≥ 0.99)
- CIDR dual prep: paper + demo drafts; decision 2026-07-20

### Innovation

- `VerifyCarlAnchorSignature` + `chain-verify --require-anchor --require-signature`
- `MaybeAutoPublishCarlAnchor` on Merkle flush / chain rotate when `EBTREE_CARL_ANCHOR_PATH` set
- STH file suffix: **`.sth.jsonl`** (implementation truth)

### Kernel

- Q3 2026: record lazy scan P50 baseline; no gate tighten
- Q4 2026: `LazyScan10kBudget` 45ms → 40ms (per lazy-scan-track doc)

## Consequences

- New gates: P16-carl-eval, P16-demo-e2e
- `demo/` CMake target; `bench/carl_eval_common` shared with audit tests
- CIDR 2027 deadline 2026-08-04 drives sprint ordering

## References

- ADR-041, Phase 2 plan
- [lazy-scan-track-2026-07-01.md](../archive/perf/lazy-scan-track-2026-07-01.md)
