# ADR-037: Histogram Summary On-Disk Format

## Status

Accepted (2026-07-01)

## Context

ADR-008 deferred histogram summary type. `kSummaryTypeHistogram = 2` exists in `page_format.h`; `prefer_histogram_summary` is set in ProductionDefaults but was under-tested.

## Decision

### Format

- PageHeader.summary_type = `kSummaryTypeHistogram` (2)
- 8 bins (`kHistogramBinCount`) over key prefix space for pruning
- Coexists with Trie threshold logic: histogram preferred when `prefer_histogram_summary=true` and fan-out ≥ threshold

### Behavior

- `PageSummaryCoversKey` uses histogram bounds before descending
- Old MinMax/Trie pages remain readable (format bit in summary_type)
- Checkpoint builds histogram incrementally from child summaries

### Verification

- matrix `paged` cases: `pages_touched` upper bound
- no_fallback: scan after reopen with histogram enabled

## References

- ADR-008, ADR-015, ADR-014
