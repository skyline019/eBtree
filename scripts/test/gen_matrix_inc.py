#!/usr/bin/env python3
"""Generate test/matrix/*_matrix_inc.h from test/data/matrix/*.matrix"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def parse_ops(block: list[str]) -> list[str]:
    for line in block:
        stripped = line.strip()
        if stripped.startswith("ops:"):
            raw = stripped.split(":", 1)[1].strip()
            if not raw:
                return []
            return [op.strip() for op in raw.split(";") if op.strip()]
    return []


def parse_case(chunk: str) -> dict:
    lines = [ln.rstrip() for ln in chunk.strip().splitlines() if ln.strip()]
    case: dict = {
        "id": "",
        "durability": "sync",
        "setup_ops": [],
        "run": "",
        "expect": "ok",
        "get_key": "",
        "get_value": "",
        "error_contains": "",
        "corrupt": "",
        "assert_stat": "",
        "compress_pages": False,
        "product_default": False,
    }
    section = None
    section_lines: list[str] = []
    for line in lines:
        stripped = line.strip()
        if stripped == "setup:":
            if section == "setup":
                case["setup_ops"] = parse_ops(section_lines)
            section = "setup"
            section_lines = []
            continue
        if stripped.startswith("case:"):
            case["id"] = stripped.split(":", 1)[1].strip()
            continue
        if stripped.startswith("run:"):
            if section == "setup":
                case["setup_ops"] = parse_ops(section_lines)
            section = None
            case["run"] = stripped.split(":", 1)[1].strip()
            continue
        if stripped.startswith("expect:"):
            case["expect"] = stripped.split(":", 1)[1].strip()
            continue
        if stripped.startswith("get:"):
            kv = stripped.split(":", 1)[1].strip()
            if "=" in kv:
                k, v = kv.split("=", 1)
                case["get_key"] = k.strip()
                case["get_value"] = v.strip()
            else:
                case["get_key"] = kv
            continue
        if stripped.startswith("error_contains:"):
            case["error_contains"] = stripped.split(":", 1)[1].strip()
            continue
        if stripped.startswith("durability:"):
            case["durability"] = stripped.split(":", 1)[1].strip()
            continue
        if stripped.startswith("assert_stat:"):
            case["assert_stat"] = stripped.split(":", 1)[1].strip()
            continue
        if stripped.startswith("corrupt:"):
            case["corrupt"] = stripped.split(":", 1)[1].strip()
            continue
        if stripped.startswith("compress_pages:"):
            case["compress_pages"] = stripped.split(":", 1)[1].strip().lower() in (
                "1",
                "true",
                "yes",
            )
            continue
        if stripped.startswith("compress:") or stripped.startswith("product_default:"):
            key = stripped.split(":", 1)[0]
            val = stripped.split(":", 1)[1].strip().lower()
            case["product_default"] = val in ("1", "true", "yes")
            continue
        if section == "setup":
            section_lines.append(line)
    if section == "setup":
        case["setup_ops"] = parse_ops(section_lines)
    return case


def symbol_prefix(stem: str) -> str:
    parts = re.split(r"[_\-]", stem)
    return "".join(p[:1].upper() + p[1:] for p in parts if p)


def emit_cases(cases: list[dict], out_path: Path, stem: str) -> None:
    prefix = symbol_prefix(stem)
    array_name = f"k{prefix}MatrixCases"
    count_name = f"k{prefix}MatrixCaseCount"
    lines = [
        "#pragma once",
        "",
        '#include "matrix_case.h"',
        "",
        f"inline const EbMatrixCase {array_name}[] = {{",
    ]
    for c in cases:
        ops = ", ".join(f'"{op}"' for op in c["setup_ops"]) or ""
        lines.append(
            f'    {{"{c["id"]}", "{c["durability"]}", {{{ops}}}, '
            f'"{c["run"]}", "{c["expect"]}", '
            f'"{c["get_key"]}", "{c["get_value"]}", '
            f'"{c["error_contains"]}", "{c["corrupt"]}", '
            f'"{c["assert_stat"]}", {str(c["compress_pages"]).lower()}, '
            f'{str(c["product_default"]).lower()}}},'
        )
    lines.extend(
        [
            "};",
            "",
            f"inline constexpr int {count_name} = {len(cases)};",
            "",
        ]
    )
    out_path.write_text("\n".join(lines), encoding="utf-8")


def generate_matrix(matrix_file: Path, out_file: Path) -> int:
    text = matrix_file.read_text(encoding="utf-8")
    chunks = [c for c in text.split("---") if c.strip()]
    cases = [parse_case(c) for c in chunks]
    emit_cases(cases, out_file, matrix_file.stem)
    print(f"Wrote {out_file} ({len(cases)} cases)")
    return len(cases)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--repo",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    parser.add_argument(
        "--matrix",
        type=str,
        default="",
        help="Matrix stem or filename under test/data/matrix (default: all)",
    )
    args = parser.parse_args()
    matrix_dir = args.repo / "test" / "data" / "matrix"
    out_dir = args.repo / "test" / "matrix"

    if args.matrix:
        stem = Path(args.matrix).stem
        matrix_file = matrix_dir / f"{stem}.matrix"
        out_file = out_dir / f"{stem}_matrix_inc.h"
        generate_matrix(matrix_file, out_file)
        return

    total = 0
    for matrix_file in sorted(matrix_dir.glob("*.matrix")):
        out_file = out_dir / f"{matrix_file.stem}_matrix_inc.h"
        total += generate_matrix(matrix_file, out_file)
    print(f"Generated {total} total cases")


if __name__ == "__main__":
    main()
