# CARL Artifact (minimal reproducible subset)

## Prerequisites

- Windows / MSVC build (`build-msvc-2026`)
- Release configuration

## Steps

1. Build audit + demo targets
2. Run gate `P15-carl-complete`
3. Run one vertical demo scenario
4. Attach gate log + `chain-verify --require-anchor` output

## Commands

```powershell
cmake --build build-msvc-2026 --config Release
.\scripts\test\run_tests.ps1 -Gate P15-carl-complete -Config Release
.\demo\run_scenario.ps1 -Scenario industrial -Config Release
```

## Included

- `ebtree_audit chain-verify --require-anchor`
- `ebtree_audit chain-anchor --publish`
- `ebtree_audit chain-proof --seq N`
- Demo scenarios (industrial / medical / finance)

## Excluded (per plan)

- Full SQL sqllogic 120k
- Distributed replication
