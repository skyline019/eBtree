"""Parse SQLite official sqllogictest format into isolated runner cases."""

from __future__ import annotations

from pathlib import Path
from typing import Iterator


def _strip_sql(lines: list[str]) -> str:
    text = "\n".join(lines).strip()
    if text.endswith(";"):
        text = text[:-1].strip()
    return text


def _is_control_line(stripped: str) -> bool:
    low = stripped.lower()
    return low.startswith(
        (
            "statement",
            "query",
            "halt",
            "onlyif",
            "skipif",
            "hash-threshold",
            "control",
        )
    )


def _engine_includes_onlyif(tag: str) -> bool:
    t = tag.lower().strip()
    if t in {"sqlite", "ebtree"}:
        return True
    return False


def _engine_excludes_skipif(tag: str) -> bool:
    # We are not mysql/mssql/oracle/postgresql; skipif never excludes for us.
    return False


def iter_cases_from_file(path: Path, source_stem: str) -> Iterator[dict]:
    """Expand stateful sqllogic file into isolated cases for ebtree runner."""
    text = path.read_text(encoding="utf-8", errors="replace")
    raw_lines = text.splitlines()
    setup: list[str] = []
    case_idx = 0
    pending_onlyif: str | None = None
    pending_skipif: str | None = None
    pending_label = ""

    def consume_gate() -> bool:
        nonlocal pending_onlyif, pending_skipif, pending_label
        if pending_skipif and _engine_excludes_skipif(pending_skipif):
            pending_onlyif = None
            pending_skipif = None
            pending_label = ""
            return False
        if pending_onlyif and not _engine_includes_onlyif(pending_onlyif):
            pending_onlyif = None
            pending_skipif = None
            pending_label = ""
            return False
        pending_onlyif = None
        pending_skipif = None
        pending_label = ""
        return True

    i = 0
    while i < len(raw_lines):
        stripped = raw_lines[i].strip()
        i += 1
        if not stripped:
            continue
        if stripped.startswith("#"):
            continue
        if stripped.startswith("-"):
            continue

        lower = stripped.lower()
        if lower.startswith("onlyif"):
            pending_onlyif = stripped.split(maxsplit=1)[1].split("#", 1)[0].strip()
            continue
        if lower.startswith("skipif"):
            pending_skipif = stripped.split(maxsplit=1)[1].split("#", 1)[0].strip()
            continue
        if lower.startswith("halt"):
            if pending_onlyif and _engine_includes_onlyif(pending_onlyif):
                return
            pending_onlyif = None
            continue
        if lower.startswith("hash-threshold") or lower.startswith("control"):
            continue

        if lower.startswith("statement"):
            if not consume_gate():
                # Still consume SQL body for skipped statement.
                while i < len(raw_lines):
                    nxt = raw_lines[i]
                    if not nxt.strip():
                        i += 1
                        break
                    if _is_control_line(nxt.strip()):
                        break
                    i += 1
                continue
            parts = stripped.split()
            expect = parts[1] if len(parts) > 1 else "ok"
            sql_lines: list[str] = []
            while i < len(raw_lines):
                nxt = raw_lines[i]
                if not nxt.strip():
                    i += 1
                    break
                if _is_control_line(nxt.strip()):
                    break
                sql_lines.append(nxt)
                i += 1
            sql = _strip_sql(sql_lines)
            if not sql:
                continue
            if expect == "ok":
                setup.append(sql)
            else:
                case_idx += 1
                name = pending_label or f"{source_stem}_{case_idx}"
                yield {
                    "name": name,
                    "setup": list(setup),
                    "sql": sql,
                    "expected": [],
                    "error": True,
                    "sort": "",
                    "source": str(path),
                }
            continue

        if lower.startswith("query"):
            if not consume_gate():
                while i < len(raw_lines):
                    nxt = raw_lines[i]
                    if nxt.strip() == "----":
                        i += 1
                        break
                    if _is_control_line(nxt.strip()):
                        break
                    i += 1
                while i < len(raw_lines):
                    nxt = raw_lines[i]
                    if not nxt.strip() or _is_control_line(nxt.strip()):
                        break
                    i += 1
                continue
            sort = ""
            coltypes = ""
            parts = stripped.split()
            if len(parts) > 1:
                coltypes = parts[1]
            for tok in parts[2:]:
                if tok.lower() in {"nosort", "rowsort", "valuesort"}:
                    sort = tok.lower()
                elif tok.startswith("label-"):
                    pending_label = f"{source_stem}_{tok}"
            sql_lines: list[str] = []
            while i < len(raw_lines):
                nxt = raw_lines[i]
                if nxt.strip() == "----":
                    i += 1
                    break
                if _is_control_line(nxt.strip()):
                    break
                sql_lines.append(nxt)
                i += 1
            expected: list[str] = []
            while i < len(raw_lines):
                nxt = raw_lines[i]
                if not nxt.strip() or _is_control_line(nxt.strip()):
                    break
                expected.append(nxt.rstrip("\n"))
                i += 1
            sql = _strip_sql(sql_lines)
            if not sql:
                continue
            case_idx += 1
            name = pending_label or f"{source_stem}_{case_idx}"
            pending_label = ""
            yield {
                "name": name,
                "setup": list(setup),
                "sql": sql,
                "expected": expected,
                "error": False,
                "sort": sort,
                "coltypes": coltypes,
                "source": str(path),
            }
            continue


def case_to_test_lines(case: dict) -> str:
    lines = [f"-- name: {case['name']}", f"-- source: {case['source']}"]
    if case.get("coltypes"):
        lines.append(f"-- coltypes: {case['coltypes']}")
    if case.get("sort"):
        lines.append(f"-- sort: {case['sort']}")
    for s in case["setup"]:
        lines.append(f"-- setup: {s}")
    if case.get("error"):
        lines.append("-- error")
    lines.append(case["sql"])
    if case.get("expected"):
        lines.append("----")
        lines.extend(case["expected"])
    lines.append("---")
    return "\n".join(lines) + "\n"
