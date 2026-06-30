#!/usr/bin/env python3
"""One-shot generator for perf.matrix expansion and parse.matrix advanced cases."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SQL_DATA = ROOT / "test" / "data" / "sql"


def chunk(case_id: str, fields: dict) -> str:
    lines = [f"case: {case_id}"]
    for key, val in fields.items():
        if key == "setups":
            for s in val:
                lines.append(f'setup: "{s}"')
        elif key in ("parse", "exec"):
            lines.append(f'{key}: "{val}"')
        else:
            lines.append(f"{key}: {val}")
    return "\n".join(lines) + "\n---\n"


def build_perf() -> str:
    perf: list[str] = []
    for n in [1, 4, 8, 16]:
        setups = ["CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)"]
        for j in range(n):
            setups.append(f"INSERT INTO t (key, value) VALUES ('k{j}', 'v{j}')")
        perf.append(
            chunk(
                f"scan_{n}_budget_zero",
                {
                    "setups": setups,
                    "exec": "SELECT key FROM t /* @max_pages=0 */",
                    "expect": "mic_violation",
                },
            )
        )
        perf.append(
            chunk(
                f"scan_{n}_budget_ok",
                {
                    "setups": setups,
                    "exec": "SELECT key FROM t /* @max_pages=32 */",
                    "expect": "ok",
                    "expect_rows": str(n),
                },
            )
        )

    join_cases = [
        ("join_budget_zero", 0, "mic_violation", -1, False),
        ("join_budget_ok", 32, "ok", 1, False),
        ("join_nomatch_ok", 32, "ok", 0, True),
        ("join_3table_zero", 0, "mic_violation", -1, False, True),
    ]
    for item in join_cases:
        cid, budget, expect, rows = item[0], item[1], item[2], item[3]
        nomatch = item[4] if len(item) > 4 else False
        three = item[5] if len(item) > 5 else False
        if three:
            setups = [
                "CREATE TABLE a (key TEXT PRIMARY KEY, value TEXT)",
                "CREATE TABLE b (key TEXT PRIMARY KEY, value TEXT)",
                "CREATE TABLE c (key TEXT PRIMARY KEY, value TEXT)",
                "INSERT INTO a (key, value) VALUES ('k1', '1')",
                "INSERT INTO b (key, value) VALUES ('k1', '1')",
                "INSERT INTO c (key, value) VALUES ('k1', '1')",
            ]
            sql = (
                f"SELECT a.key FROM a JOIN b ON a.key=b.key "
                f"JOIN c ON b.key=c.key /* @max_pages={budget} */"
            )
        else:
            setups = [
                "CREATE TABLE a (key TEXT PRIMARY KEY, value TEXT)",
                "CREATE TABLE b (key TEXT PRIMARY KEY, value TEXT)",
                "INSERT INTO a (key, value) VALUES ('k1', '1')",
                "INSERT INTO b (key, value) VALUES ('k2', '2')" if nomatch
                else "INSERT INTO b (key, value) VALUES ('k1', '1')",
            ]
            sql = f"SELECT a.key FROM a JOIN b ON a.key=b.key /* @max_pages={budget} */"
        fields: dict = {"setups": setups, "exec": sql, "expect": expect}
        if rows >= 0:
            fields["expect_rows"] = str(rows)
        perf.append(chunk(cid, fields))

    for cid, budget, expect, rows in [
        ("in_subquery_zero", 0, "mic_violation", -1),
        ("in_subquery_ok", 32, "ok", 1),
        ("in_subquery_multi_ok", 32, "ok", 2),
    ]:
        setups = [
            "CREATE TABLE a (key TEXT PRIMARY KEY, value TEXT)",
            "CREATE TABLE b (key TEXT PRIMARY KEY, value TEXT)",
            "INSERT INTO a (key, value) VALUES ('k1', '1')",
            "INSERT INTO a (key, value) VALUES ('k2', '2')",
            "INSERT INTO b (key, value) VALUES ('k1', '1')",
        ]
        sql = (
            f"SELECT a.key FROM a WHERE a.key IN (SELECT b.key FROM b) "
            f"/* @max_pages={budget} */"
        )
        fields: dict = {"setups": setups, "exec": sql, "expect": expect}
        if rows >= 0:
            fields["expect_rows"] = str(rows)
        perf.append(chunk(cid, fields))

    return "".join(perf[:20])


def build_parse_extra() -> str:
    extra: list[str] = []
    cte_sqls = [
        "WITH cte AS (SELECT key FROM t) SELECT key FROM cte",
        "WITH a AS (SELECT k FROM t1) SELECT k FROM a",
        "WITH x AS (SELECT key FROM t) SELECT key FROM x WHERE key = 'a'",
        "WITH c1 AS (SELECT k FROM a), c2 AS (SELECT k FROM b) SELECT c1.k FROM c1",
        "WITH r AS (SELECT key, value FROM t) SELECT value FROM r",
        "WITH w AS (SELECT key FROM t WHERE key > 'a') SELECT key FROM w",
        "WITH cte AS (SELECT key FROM t) SELECT key FROM cte WHERE key = 'k1'",
        "WITH inner AS (SELECT key FROM s) SELECT key FROM inner",
        "WITH z AS (SELECT a FROM t1) SELECT a FROM z",
        "WITH p AS (SELECT key FROM t) SELECT COUNT(key) FROM p",
        "WITH q AS (SELECT key FROM t) SELECT key FROM q ORDER BY key",
        "WITH m AS (SELECT key FROM t) SELECT key FROM m LIMIT 1",
        "WITH n AS (SELECT key FROM t) SELECT key FROM n OFFSET 0",
        "WITH o AS (SELECT key FROM t) SELECT DISTINCT key FROM o",
        "WITH u AS (SELECT key FROM t) SELECT key FROM u UNION SELECT key FROM t",
    ]
    for i, sql in enumerate(cte_sqls):
        extra.append(chunk(f"cte_{i:02d}", {"parse": sql, "expect_kind": "kWithCte"}))

    setop_templates = [
        ("union", "SELECT a FROM t1 UNION SELECT b FROM t2"),
        ("union_all", "SELECT a FROM t1 UNION ALL SELECT b FROM t2"),
        ("intersect", "SELECT a FROM t1 INTERSECT SELECT b FROM t2"),
        ("except", "SELECT a FROM t1 EXCEPT SELECT b FROM t2"),
    ]
    for i in range(15):
        kind, sql = setop_templates[i % 4]
        t1, t2 = f"t{i * 2 + 1}", f"t{i * 2 + 2}"
        sql = sql.replace("t1", t1).replace("t2", t2)
        extra.append(
            chunk(f"setop_{i:02d}_{kind}", {"parse": sql, "expect_kind": "kSetOp"})
        )

    windows = [
        "SELECT ROW_NUMBER() OVER (ORDER BY a) FROM t",
        "SELECT RANK() OVER (ORDER BY a) FROM t",
        "SELECT DENSE_RANK() OVER (ORDER BY a) FROM t",
        "SELECT SUM(a) OVER (ORDER BY a) FROM t",
        "SELECT AVG(a) OVER (ORDER BY a) FROM t",
        "SELECT COUNT(a) OVER (ORDER BY a) FROM t",
        "SELECT MIN(a) OVER (ORDER BY a) FROM t",
        "SELECT MAX(a) OVER (ORDER BY a) FROM t",
        "SELECT ROW_NUMBER() OVER (PARTITION BY b ORDER BY a) FROM t",
        "SELECT ROW_NUMBER() OVER (ORDER BY a DESC) FROM t",
    ]
    for i, sql in enumerate(windows):
        extra.append(
            chunk(f"window_{i:02d}", {"parse": sql, "expect_kind": "kWindowSelect"})
        )

    txn = [
        ("begin", "BEGIN", "kBeginTxn"),
        ("begin_txn", "BEGIN TRANSACTION", "kBeginTxn"),
        ("commit", "COMMIT", "kCommit"),
        ("rollback", "ROLLBACK", "kRollback"),
        ("savepoint", "SAVEPOINT sp1", "kSavepoint"),
        ("show_tables", "SHOW TABLES", "kShow"),
        ("set_var", "SET autocommit = 1", "kSet"),
        ("grant", "GRANT SELECT ON t TO user1", "kGrant"),
        ("explain", "EXPLAIN SELECT * FROM t", "kExplain"),
        (
            "nested_exists",
            "SELECT a.key FROM a WHERE EXISTS (SELECT b.key FROM b WHERE b.key = a.key "
            "AND EXISTS (SELECT a.key FROM a WHERE a.key = b.key))",
            "kSelect",
        ),
    ]
    for cid, sql, ek in txn:
        fields: dict = {"parse": sql, "expect_kind": ek}
        if cid == "nested_exists":
            fields["expect_subquery"] = "1"
        extra.append(chunk(f"adv_{cid}", fields))

    return "".join(extra)


def main() -> None:
    perf_path = SQL_DATA / "perf.matrix"
    perf_path.write_text(build_perf(), encoding="utf-8")
    print("perf cases:", perf_path.read_text(encoding="utf-8").count("case:"))

    parse_path = SQL_DATA / "parse.matrix"
    base = parse_path.read_text(encoding="utf-8")
    if not base.rstrip().endswith("---"):
        base = base.rstrip() + "\n---\n"
    parse_path.write_text(base + build_parse_extra(), encoding="utf-8")
    print("parse cases:", parse_path.read_text(encoding="utf-8").count("case:"))


if __name__ == "__main__":
    main()
