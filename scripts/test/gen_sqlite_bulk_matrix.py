from pathlib import Path

lines = []
for i in range(120):
    lines.append(
        f"case: bulk_select_{i:03d}\n"
        f'parse: "SELECT key FROM t{i} WHERE key = \'k{i}\'"\n'
        f"expect_kind: kSelect\n"
        f"---"
    )
Path(__file__).resolve().parents[1] / "test/data/sql/sqlite_bulk.matrix"
out = Path(__file__).resolve().parents[1] / "test/data/sql/sqlite_bulk.matrix"
out.write_text("\n".join(lines) + "\n", encoding="utf-8")
print("wrote", len(lines), "cases to", out)
