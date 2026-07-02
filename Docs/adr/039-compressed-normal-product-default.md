# ADR-039: Compressed-Normal Product Default

## Status

Accepted (2026-07-01)

## Context

P9 shipped dual compress SKUs with `ProductionDefaults` compress off (ADR-035, ADR-009 bench baseline). Product intent is **kBalanced + value compression on by default** without regressing raw perf measurement or No-Fallback contracts.

## Decision

### SKU table

| Factory | Role | durability | compress_values | compress_policy |
|---------|------|------------|-----------------|-----------------|
| `StandardDefaults` | **Product / SQL / CLI default** | kBalanced | true | kFastOnly |
| `ProductionDefaults` | Bench / ADR-009 raw baseline | kBalanced | false | kOff |
| `ProductionCompressDefaults` | Explicit BalancedCompress SKU | kBalanced | true | kFastOnly |
| `EnterpriseDefaults` | Compliance kSync | kSync | false | kOff |
| `EnterpriseCompressDefaults` | Space-efficient SKU | kSync | true | kBalanced + pages |

`StandardDefaults(path)` is an alias of `ProductionCompressDefaults(path)`.

### Invariants

- WAL records remain uncompressed (ADR-024).
- `ProductionDefaults` stays raw for `ebtree_write_bench` and raw perf gates.
- SQL `OpenOptions::ToEngineOptions()` uses `StandardDefaults` for kBalanced.
- Opt-out: open with `ProductionDefaults` or `compress_values=false`.

### Verification

- P14-standard-product / P14-powerfail-compress gates
- Dual perf track: raw (P9-perf) vs Standard (P14-standard-perf)
- `no_fallback.matrix` compress cases assert `unexpected_path_total=0`

## References

- ADR-009, ADR-035, ADR-034, ADR-021
