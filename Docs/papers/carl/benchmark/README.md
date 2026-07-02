# CARL Benchmark Harness

Runs as part of `ebtree_tests_audit` (`CarlBenchmark*`).

## Metrics

| Metric | Source |
|--------|--------|
| Chain verify latency | `VerifyRarChain` on N entries |
| Anchor publish latency | `PublishCarlAnchor` |
| Merkle proof generation | `GenerateCarlMerkleProof` |
| Tamper detection | `CarlAnchorTamperDetect` |

## Reproduce

```powershell
cmake --build build-msvc-2026 --config Release --target ebtree_tests_audit
.\build-msvc-2026\Release\ebtree_tests_audit.exe --gtest_filter=CarlBenchmark*
```

## Baseline comparison

Compare against no-CARL engine open (attestation off) using `EbPipelinePerf.KBalancedWrite100kWithRarMonitor` (Release gate).
