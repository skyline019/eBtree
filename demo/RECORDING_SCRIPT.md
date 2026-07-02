# 工控 CARL Demo — 5 分钟录屏脚本

**目标**：CIDR 2027 Demo track 素材  
**前置**：Release 构建 + `P16-demo-e2e` 绿

## 环境

```powershell
cmake --build build-msvc-2026 --config Release `
  --target ebtree_demo_industrial ebtree_audit
```

## 分镜（≈5 分钟）

| 时间 | 画面 | 旁白要点 |
|------|------|----------|
| 0:00–0:45 | 问题 slide：边缘配置库、crash、本地 admin 可改 chain | tamper-evident，非 tamper-proof |
| 0:45–2:00 | 运行 `.\demo\run_scenario.ps1 -Scenario industrial` | STEP_OK 逐步出现 |
| 2:00–2:45 | 高亮 `chain-verify --require-anchor` 输出 | 外部 STH 对齐 chain tail |
| 2:45–3:30 | 展示 `STEP_OK sql_write_circuit` + SELECT 仍可用 | MONITOR 写熔断、读可用 |
| 3:30–4:15 | （可选）手动改 chain 一行 → verify 失败 | 篡改检测 |
| 4:15–5:00 | 切换 medical/finance 一行命令 | 同一 CARL 内核，三垂直 |

## 命令清单

```powershell
cd E:\DBProject
.\demo\run_scenario.ps1 -Scenario industrial -BuildDir build-msvc-2026 -Config Release
.\build-msvc-2026\Release\ebtree_audit.exe chain-verify --path demo_data_industrial --require-anchor
.\demo\run_scenario.ps1 -Scenario medical -BuildDir build-msvc-2026 -Config Release
.\demo\run_scenario.ps1 -Scenario finance -BuildDir build-msvc-2026 -Config Release
```

## 检查清单（vertical-playbooks）

- [ ] 初始 `allows_write=1`（medical PRAGMA）
- [ ] chain `consistent=true`
- [ ] anchor 与 tail 一致
- [ ] 违规后 SQL INSERT 失败、SELECT 可用

录屏完成后将链接填入 [`cidr-demo-submission.md`](../Docs/papers/carl/cidr-demo-submission.md)。
