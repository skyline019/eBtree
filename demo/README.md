# eB-Tree 垂直 Demo

三套可切换 scenario，共用 CARL 内核（MONITOR + async chain + anchor）。

## 运行

构建 demo 可执行文件后：

```powershell
cmake --build build-msvc-2026 --config Release --target ebtree_demo_industrial ebtree_demo_medical ebtree_demo_finance ebtree_audit
.\demo\run_scenario.ps1 -Scenario industrial|medical|finance -BuildDir build-msvc-2026 -Config Release
```

或直接运行：`ebtree_demo_industrial.exe <data_dir>`

## Scenarios

| Scenario | 路径 | 演示要点 |
|----------|------|----------|
| industrial | `demo/industrial_config/` | KV 写入 → crash → FastOpen → chain verify → 写熔断 |
| medical | `demo/medical_record/` | SQL INSERT + PRAGMA rar_status + anchor publish |
| finance | `demo/finance_edge/` | kSync Open + REQUIRE_PASS vs MONITOR 对比 |

## 5 分钟录屏脚本

1. 打开终端，运行 `run_scenario.ps1 -Scenario industrial`
2. 展示 checkpoint 后 `ebtree_audit chain-verify --require-anchor`
3. 模拟 tamper（修改 chain 一行）→ verify 失败
4. 切换 medical：展示 `PRAGMA rar_status` 与 anchor 字段
5. 切换 finance：对比 MONITOR 默认与 opt-in REQUIRE_PASS

## 环境变量

- `EBTREE_CARL_ANCHOR_PATH` — 外部 anchor 目录（WORM / 网络挂载）
- `EBTREE_RAR_KEY` — 可选 Ed25519 签名（需 `EBTREE_RAR_SIGNING=1` 构建）
