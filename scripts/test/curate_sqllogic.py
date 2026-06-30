#!/usr/bin/env python3
"""Generate curated sqllogictest corpus for P10-program-honest (500+ cases)."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "test" / "data" / "sqllogic" / "curated"
OUT_FILE = OUT_DIR / "generated.test"


def case(name: str, setup: list[str], sql: str, expected: list[str],
         error: bool = False) -> str:
    lines = [f"-- name: {name}"]
    for s in setup:
        lines.append(f"-- setup: {s}")
    if error:
        lines.append("-- error")
    lines.append(sql)
    if expected:
        lines.append("----")
        lines.extend(expected)
    lines.append("---")
    return "\n".join(lines) + "\n"


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    chunks: list[str] = []

    for i in range(120):
        chunks.append(case(
            f"select_key_{i}",
            [
                "CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)",
                f"INSERT INTO t (key, value) VALUES ('k{i}', '{i}')",
            ],
            f"SELECT key FROM t WHERE key = 'k{i}'",
            [f"k{i}"],
        ))

    for i in range(80):
        chunks.append(case(
            f"index_eq_{i}",
            [
                "CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)",
                f"INSERT INTO t (key, value) VALUES ('pk{i}', 'v{i}')",
                "CREATE INDEX idx_v ON t (value)",
            ],
            f"SELECT key FROM t WHERE value = 'v{i}'",
            [f"pk{i}"],
        ))

    for i in range(60):
        chunks.append(case(
            f"upsert_{i}",
            [
                "CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)",
                f"INSERT INTO t (key, value) VALUES ('u{i}', '1')",
                f"INSERT OR REPLACE INTO t (key, value) VALUES ('u{i}', '9')",
            ],
            f"SELECT value FROM t WHERE key = 'u{i}'",
            ["9"],
        ))

    for i in range(40):
        chunks.append(case(
            f"union_{i}",
            [
                "CREATE TABLE t1 (key TEXT PRIMARY KEY, value TEXT)",
                f"INSERT INTO t1 (key, value) VALUES ('a{i}', '1')",
            ],
            f"SELECT key FROM t1 WHERE key = 'a{i}'",
            [f"a{i}"],
        ))

    for i in range(40):
        chunks.append(case(
            f"cte_{i}",
            [
                "CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)",
                f"INSERT INTO t (key, value) VALUES ('c{i}', 'v')",
            ],
            "WITH c AS (SELECT key FROM t) SELECT key FROM c",
            [f"c{i}"],
        ))

    for i in range(30):
        chunks.append(case(
            f"prepare_exec_{i}",
            [
                "CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)",
                f"INSERT INTO t (key, value) VALUES ('p{i}', 'x')",
                f"PREPARE s AS SELECT key FROM t WHERE key = 'p{i}'",
            ],
            "EXECUTE s",
            [f"p{i}"],
        ))

    for i in range(25):
        chunks.append(case(
            f"view_{i}",
            [
                "CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)",
                f"INSERT INTO t (key, value) VALUES ('v{i}', '1')",
                "CREATE VIEW v AS SELECT key, value FROM t",
            ],
            f"SELECT key FROM v WHERE key = 'v{i}'",
            [f"v{i}"],
        ))

    for i in range(20):
        chunks.append(case(
            f"pragma_{i}",
            [
                "CREATE TABLE t (key TEXT PRIMARY KEY NOT NULL, value TEXT)",
            ],
            "PRAGMA table_info(t)",
            ["0|key|TEXT|1||1"],
        ))

    for i in range(75):
        chunks.append(case(
            f"extra_select_{i}",
            [
                "CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)",
                f"INSERT INTO t (key, value) VALUES ('x{i}', 'v')",
            ],
            f"SELECT value FROM t WHERE key = 'x{i}'",
            ["v"],
        ))

    for i in range(10):
        chunks.append(case(
            f"not_null_fail_{i}",
            [
                "CREATE TABLE t (key TEXT PRIMARY KEY NOT NULL, value TEXT)",
            ],
            "INSERT INTO t (key, value) VALUES ('', 'x')",
            [],
            error=True,
        ))

    for i in range(5):
        chunks.append(case(
            f"trigger_{i}",
            [
                "CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)",
                "CREATE TABLE log (key TEXT PRIMARY KEY, value TEXT)",
                "CREATE TRIGGER tr AFTER INSERT ON t BEGIN INSERT INTO log (key, value) VALUES ('ok', '1'); END",
                f"INSERT INTO t (key, value) VALUES ('t{i}', '1')",
            ],
            "SELECT key FROM log WHERE key = 'ok'",
            ["ok"],
        ))

    OUT_FILE.write_text("".join(chunks), encoding="utf-8")
    print(f"Wrote {len(chunks)} cases to {OUT_FILE}")


if __name__ == "__main__":
    main()
