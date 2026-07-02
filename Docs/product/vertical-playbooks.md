# 垂直场景 Playbook

同一 CARL 内核，三套演示场景。运行：`demo/run_scenario.ps1 -Scenario industrial|medical|finance`

## 工控 / 边缘配置库

**User story**：PLC 网关本地持久化配置版本；断网可写；重启需证明未静默损坏。

**路径**：KV Put → Checkpoint → CARL append → crash → FastOpen → `chain-verify` → inject `unexpected_path_total` → KV 写熔断 → **SQL INSERT 熔断 + SELECT 可用**

**合规映射**：IEC 62443 审计日志（tamper-evident，非 cryptographic proof alone）

## 医疗本地记录

**User story**： bedside 设备本地记录；需可追溯；离线运行。

**路径**：SQL INSERT → `PRAGMA rar_status` → `chain-anchor` → 外部 STH 目录

**合规映射**：FDA 21 CFR Part 11 计算机化系统审计（detect alteration；需 external anchor + 流程）

## 金融边缘节点

**User story**：网点边缘库；强耐久；升级时可 REQUIRE_PASS Open。

**路径**：kSync engine → 对比 MONITOR 默认 vs `REQUIRE_PASS` Open → chain consistent

**合规映射**：SOC2 CC7.2 变更检测；kSync RPO 叙事

## 演示检查清单

- [ ] `allows_write=1` 初始
- [ ] chain `consistent=true`
- [ ] anchor STH 与 chain tail 一致（finance/medical）
- [ ] 违规后 `allows_write=0`，SELECT 仍可用
