# ADR-045: Phase 5 Lazy Scan

## Status

Accepted (2026-07-02)

Unblocked by ADR-044 streak **ACCEPT** (10/10 green, `streak-20260702-001854`).

## Decision

### Code

1. `DataFile::ReadRecordsAtOffsets` — mmap `PinWindow` batch + stream blob coalesce (256KB cap)
2. `ShardEngine::ResolveScanValues` — dedicated `direct_disk_scan` fast path (lazy reopen)

### Gate

| Metric | Local Release | CI (`EBTEST_CI`) |
|--------|---------------|------------------|
| Lazy scan 10k | **≤40ms** (was 45ms) | ≤60ms |
| Standard lazy ratio | ≤1.10× raw + 2ms | ≤1.25× + 5ms |

### Verification

```powershell
# Perf only — do not require P18 streak re-run for Phase 5 code changes
.\scripts\test\run_tests.ps1 -Gate P17-deep-core -Config Release
```

## References

- ADR-043, ADR-044, ADR-018
- [lazy-scan-track-2026-07-01.md](../archive/perf/lazy-scan-track-2026-07-01.md)
