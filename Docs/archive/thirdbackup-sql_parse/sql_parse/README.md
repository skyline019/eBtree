# sql_parse（概念–同步式 SQL 前端）

**生产入口**：`SqlParser::Parse` → `SqlParseFacade`（`ParseBootstrap` + Lex/Stmt 注册表）。  
**内部**：`ParseSqlStatement` 同样委托 Facade；`ParseSqlStatementViaRouter` 仅用于注册表 fallback 与等价测试。  
旧双入口备份已移除，见 [`backup/README.md`](backup/README.md)。

## 分层（见 [PARSE_CONCEPT_SYNC.md](../../Docs/sql_parse/PARSE_CONCEPT_SYNC.md)）

| 目录 | 层 |
|------|-----|
| `bootstrap/` `core/` `lex/` `stmt/routes/` | 编排（Orchestration） |
| `shared/` `expr/` `stmt/{select,dml,ddl,where,dispatch}/` | 概念（ParseConcept） |
| `stmt/select/` | `select_core` / `select_project` / `select_from` / `select_group` + cte/set_ops/tail |
| `stmt/dml/` | `parse_dml.cc` + `parse_stmt_dml.cc` |
| `stmt/ddl/` | `parse_ddl.cc` + `parse_stmt_ddl.cc` |
| `pred/` | WHERE/HAVING 单入口 `pred::ParseWhere` / `pred::ParseHaving` |
| `sql_parse_facade.*` | 对外 Facade |

## 测试

```text
heterodb_tests --gtest_filter=SqlParse*:*Parser*:*parser*
scripts/check_parse_deps.sh
```

规则清单：[PARSE_RULE_MANIFEST.yaml](../../Docs/sql_parse/PARSE_RULE_MANIFEST.yaml)

## 扩展 Cookbook

1. **契约**：在 `Docs/SQL_SYNTAX_MANIFEST.yaml` 增加 topic（`example` + `test_ref`），运行 `python scripts/sync_sql_reference.py`。
2. **新顶层语句**：`*_api.h` → `stmt/routes/stmt_rule_*.cc` → `parse_stmt_handlers.cc` → `dispatch_*` 或 `parse_stmt_*`；同步 `PARSE_RULE_MANIFEST.yaml`。
3. **新子句**（SELECT/DML/DDL 内部）：在 `stmt/select/`、`stmt/dml/`、`stmt/ddl/` 等领域模块新增 `parse_*.cc`；WHERE/HAVING 只经 `pred::ParseWhere` / `ParseHaving`；表达式只经 `ParseExpr`。
4. **ALTER 批量**：`ddl/alter_actions.cc` 中 `ParseSingleAlterAction` + `ParseAlterActionLoop`。
5. **JOIN ON**：`where/join_on_parse.cc` 中 `ParseJoinOnCondition`（复合等值 AND）。
6. **标识符**：反引号统一在 `shared/ident_normalize.cc`（`NormalizeIdent`），不散落字符串处理。
7. **新函数（Phase 92+）**：在 `expr/sql_func_registry.cc` 对应分类表登记 `eval_supported`；在 `concept/query/expr.cc` 的 `kFunc` 分支实现 eval；`parser_func_eval_parity_test` 的 `ScalarExprFor` 补样例 SQL；`python scripts/sync_sql_reference.py`。
8. **语句头路由**：`stmt/stmt_head_table.cc` 为 SSOT；`dispatch_router.cc` 与 `stmt_rule_*.cc` 须保持一致。
9. **测试**：`test/unit/sql_parse/parser_<feature>_test.cc` + AST 形状断言；路由改动必跑 `SqlParseDeepEquiv*`；`scripts/check_parse_deps.sh`。

## WHERE 单轨契约（Phase 8）

| 字段 | 用途 |
|------|------|
| `where_expr` | **canonical** — 所有 WHERE 均构建表达式树 |
| `where_and` / `where_or` / `key_range` | **derived** — `ApplyWhereCanonicalize` 从 `where_expr` lowering 派生，供 executor / 快路径 |
| `ApplyWhereCanonicalize` | Parse 末尾：`BuildWhereExprIfMissing` + `LowerWhereExpr` + normalize |

标量子查询、NOT IN、函数谓词均可进入 `where_expr`；可索引谓词由 lowering 回填 `where_and`。HAVING 对称见 Phase 9。Phase 10–14：CHECK/Pratt/OR 契约/CI fuzz（见各 `parser_phase*_test.cc`）。**Phase 15–18**：嵌套 `subquery_stmt`、`set_op_chain_*`、WITH→DML、窗口+GROUP BY、READ UNCOMMITTED、Admin parse（`SqlParsePhase15*`–`18*`）。**Phase 24–32**：SSOT 文档、AST 单写、执行对齐、`parse_error_kind`、质量门禁（见 [SQL_PARSER_ANALYSIS.md](../../Docs/SQL_PARSER_ANALYSIS.md)、[ADR 049](../../Docs/adr/049-mysql-dialect-gap.md)）。

```text
heterodb_tests --gtest_filter=SqlParse*:SqlSyntaxManifestTest*:CheckConstraint*:CatalogCheckCodec*:SqlParsePhase*:ParseErrorKindAudit*:ParserFuzzParseError*:SqlParseFuncEvalParity*
python scripts/sync_sql_reference.py --check-matrix-inc
scripts/check_parse_deps.sh
python scripts/sync_sql_reference.py --check
cmake -DHETERODB_BUILD_PARSE_FUZZ=ON && ./sql_parse_fuzz
# optional (Clang): cmake -DHETERODB_LIBFUZZER=ON && ./sql_parse_libfuzzer -runs=10000
```
