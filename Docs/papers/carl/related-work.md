# CARL Related Work

| System | Scope | Attestation | External anchor | Perf claim |
|--------|-------|-------------|-----------------|------------|
| **CARL (eB-Tree)** | Embedded OLTP checkpoint | Hash chain + Merkle batch | STH `.sth.jsonl` | ≥0.99× write (MONITOR) |
| GlassDB | Verifiable DB | Merkle B-tree | Client-side verify | Not OLTP-neutral |
| Amazon QLDB | Ledger DB | Digest chain | Journal export | Cloud-only |
| PoWER / CapybaraKV | KV recovery | WAL attest | None (local) | Recovery-focused |
| CT (RFC 6962) | Certificate transparency | Merkle tree | Signed tree head | N/A (reference) |
| ARIES | Recovery | LSN redo/undo | None | Industry baseline |

## Positioning

CARL targets **perf-neutral recovery attestation** for embedded engines: async worker chain, optional external STH, Merkle batch proofs — without distributed consensus or full ledger semantics.

## Threat model summary

- **Crash**: chain continuity detects missing checkpoints
- **Local admin rewrite**: anchor root_hash mismatch
- **Bit-flip**: RAR hash chain + optional signature

Not in scope: remote adversary with anchor write access, Byzantine replicas.
