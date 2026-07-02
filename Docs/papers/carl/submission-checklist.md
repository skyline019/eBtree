# EuroSys / VLDB / CIDR 投稿清单

## CIDR 2027（Phase 2 冲刺）

| 项 | 日期 | 状态 |
|----|------|------|
| 决策点 Demo vs Paper | **2026-07-20** | 见 [cidr-track-decision.md](cidr-track-decision.md) |
| 截稿 | **2026-08-04** PT | |
| Paper 草稿 | [cidr-paper-draft.md](cidr-paper-draft.md) | skeleton |
| Demo 草稿 | [cidr-demo-submission.md](cidr-demo-submission.md) | skeleton |
| Eval 表 | [eval-results.md](eval-results.md) | `scripts/papers/refresh_eval_results.ps1` |
| 录屏脚本 | [demo/RECORDING_SCRIPT.md](../../demo/RECORDING_SCRIPT.md) | 待录屏 |

### CIDR Artifact

```powershell
.\scripts\test\run_tests.ps1 -Gate P16-carl-eval -Config Release
.\scripts\test\run_tests.ps1 -Gate P16-demo-e2e -Config Release
.\demo\run_scenario.ps1 -Scenario industrial -Config Release
```

## EuroSys / VLDB 首投（长文）

- [ ] 全文 8–10 页（见 [draft-outline.md](draft-outline.md)）
- [ ] Related work 表（见 [related-work.md](related-work.md)）
- [ ] Artifact plan（见 [artifact/README.md](artifact/README.md)）

| 会议 | Track | 窗口 |
|------|-------|------|
| EuroSys | Main | M9–M10 |
| VLDB | Research | M9–M10 |
| CIDR | Paper/Demo 6pp | 2026-08-04 |

## 基础 Gate

```powershell
.\scripts\test\run_tests.ps1 -Gate P15-carl-complete -Config Release
.\scripts\test\run_tests.ps1 -Gate P14-rar-product -Config Release
```

## Rebuttal / Resubmit (M12–M18)

- 按审稿意见补 formal fragment 或 Merkle consistency proof
- 扩展 benchmark vs GlassDB/PoWER 公开数字（若 artifact 可用）
