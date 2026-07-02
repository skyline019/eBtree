# CIDR 2027 Demo Submission Draft

**Title**: CARL: Tamper-Evident Recovery for Edge OLTP (Live Demo)

## Demo Summary

End-to-end **industrial edge configuration** scenario on eB-Tree:

1. KV writes + checkpoint with CARL MONITOR async chain
2. Simulated crash (process teardown without extra flush)
3. FastOpen + `chain-verify --require-anchor`
4. Runtime write circuit on policy violation (`unexpected_path_total`) — KV **and SQL INSERT**
5. Read path remains available (SELECT / Get)

录屏分镜：[`demo/RECORDING_SCRIPT.md`](../../demo/RECORDING_SCRIPT.md)

## How to Run

```powershell
cmake --build build-msvc-2026 --config Release --target ebtree_demo_industrial ebtree_audit
.\demo\run_scenario.ps1 -Scenario industrial -BuildDir build-msvc-2026 -Config Release
```

Or: `ebtree_demo_industrial.exe <data_dir>`

## Video Outline (5 min)

| Time | Content |
|------|---------|
| 0:00 | Problem: edge config DB, crash, silent corruption |
| 0:45 | Run industrial demo; show STEP_OK trace |
| 2:00 | `chain-verify --require-anchor` |
| 3:00 | Tamper chain file → verify fails (optional live) |
| 4:00 | Write circuit: KV + SQL INSERT blocked, SELECT/Get OK |
| 4:30 | Medical/finance scenarios (brief) |

## Video Link

(TBD — record after `P16-demo-e2e` green)

## Checklist (vertical-playbooks)

- [x] `allows_write=1` initially
- [x] chain `consistent=true` after reopen
- [x] anchor STH matches chain tail
- [x] violation → `allows_write=0`, read OK

## Authors

(TBD)
