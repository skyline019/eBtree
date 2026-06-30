#!/usr/bin/env python3
"""Import official SQLite sqllogictest corpus (risinglightdb mirror) for baseline."""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

from sqllogic_parse import case_to_test_lines, iter_cases_from_file

ROOT = Path(__file__).resolve().parents[2]
VENDOR_DIR = ROOT / "test" / "data" / "sqllogic" / "sqlite" / "vendor_official"
OUT_FILE = ROOT / "test" / "data" / "sqllogic" / "sqlite" / "imported.test"
MANIFEST = ROOT / "test" / "data" / "sqllogic" / "sqlite" / "sources.txt"
FILE_LIST = ROOT / "test" / "data" / "sqllogic" / "sqlite" / "official_files.manifest"
FILTER_STATS = ROOT / "test" / "data" / "sqllogic" / "sqlite" / "import_filter_stats.json"

# Pinned risinglightdb/sqllogictest-sqlite commit (reproducible imports).
GIT_COMMIT = "e4e221d8fd247f9fe46cffee0cb1fd32e550c29e"
GIT_COMMIT_SHORT = "e4e221d8"
GIT_BRANCH = "main"
REPO = "risinglightdb/sqllogictest-sqlite"

MIRROR_PRESETS: dict[str, list[str]] = {
    # Default: China-friendly CDN / proxies first.
    "cn": [
        "https://ghproxy.net/https://raw.githubusercontent.com/{repo}/{commit}/test/{rel}",
        "https://mirror.ghproxy.com/https://raw.githubusercontent.com/{repo}/{commit}/test/{rel}",
        "https://fastly.jsdelivr.net/gh/{repo}@{branch}/test/{rel}",
        "https://cdn.jsdelivr.net/gh/{repo}@{branch}/test/{rel}",
        "https://raw.githubusercontent.com/{repo}/{commit}/test/{rel}",
    ],
    "jsdelivr": [
        "https://fastly.jsdelivr.net/gh/{repo}@{branch}/test/{rel}",
        "https://cdn.jsdelivr.net/gh/{repo}@{branch}/test/{rel}",
    ],
    "ghproxy": [
        "https://ghproxy.net/https://raw.githubusercontent.com/{repo}/{commit}/test/{rel}",
        "https://mirror.ghproxy.com/https://raw.githubusercontent.com/{repo}/{commit}/test/{rel}",
    ],
    "direct": [
        "https://raw.githubusercontent.com/{repo}/{commit}/test/{rel}",
    ],
}

SKIP_SQL_RE = re.compile(
    r"\b(ATTACH|DETACH|load_extension|readfile|writefile|"
    r"CREATE VIRTUAL TABLE|USING fts[345]|USING rtree|"
    r"START WITH|GENERATED ALWAYS)\b",
    re.IGNORECASE,
)

HASH_RESULT_RE = re.compile(r"hashing to", re.IGNORECASE)


def load_official_test_files() -> list[str]:
    if not FILE_LIST.exists():
        raise FileNotFoundError(
            f"missing {FILE_LIST}; run scripts/test/import_sqlite_sqllogic.py once online"
        )
    lines = [
        ln.strip()
        for ln in FILE_LIST.read_text(encoding="utf-8").splitlines()
        if ln.strip() and not ln.startswith("#")
    ]
    return lines


def mirror_urls(rel_path: str, preset: str) -> list[str]:
    templates = MIRROR_PRESETS.get(preset, MIRROR_PRESETS["cn"])
    rel = rel_path.replace("\\", "/")
    return [
        tpl.format(
            repo=REPO,
            commit=GIT_COMMIT_SHORT,
            branch=GIT_BRANCH,
            rel=rel,
        )
        for tpl in templates
    ]


def download_file(rel_path: str, dest: Path, preset: str) -> bool:
    dest.parent.mkdir(parents=True, exist_ok=True)
    errors: list[str] = []
    for url in mirror_urls(rel_path, preset):
        try:
            req = urllib.request.Request(
                url,
                headers={"User-Agent": "ebtree-sqllogic-import/1.0"},
            )
            with urllib.request.urlopen(req, timeout=90) as resp:
                data = resp.read()
            if len(data) < 16:
                errors.append(f"{url}: empty")
                continue
            dest.write_bytes(data)
            print(f"ok {rel_path} via {url.split('/')[2]}")
            return True
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            errors.append(f"{url}: {exc}")
            time.sleep(0.2)
    print(f"skip download {rel_path}: {' | '.join(errors[:2])}", file=sys.stderr)
    return False


def case_is_in_scope(case: dict) -> tuple[bool, str]:
    sql_blob = "\n".join(case["setup"]) + "\n" + case["sql"]
    if SKIP_SQL_RE.search(sql_blob):
        return False, "out_of_scope_sql"
    if case.get("error"):
        return True, ""
    exp = case.get("expected") or []
    if not exp:
        return True, ""
    for line in exp:
        if HASH_RESULT_RE.search(line):
            return False, "hash_threshold_result"
    max_cols = max(line.count("|") + 1 for line in exp)
    coltypes = case.get("coltypes", "")
    if len(coltypes) > 1 or max_cols > 1:
        return False, "multi_column_result"
    return True, ""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--max-cases", type=int, default=800)
    parser.add_argument("--per-file-cap", type=int, default=50,
                        help="max imported cases per source file (0 = no cap)")
    parser.add_argument("--skip-download", action="store_true")
    parser.add_argument(
        "--mirror",
        choices=sorted(MIRROR_PRESETS.keys()),
        default="cn",
        help="download mirror preset (default: cn = jsdelivr + ghproxy)",
    )
    args = parser.parse_args()

    VENDOR_DIR.mkdir(parents=True, exist_ok=True)
    OUT_FILE.parent.mkdir(parents=True, exist_ok=True)

    rel_files = load_official_test_files()
    chunks: list[str] = []
    downloaded: list[str] = []
    stats = {
        "source": f"{REPO}@{GIT_COMMIT_SHORT}",
        "mirror": args.mirror,
        "files": 0,
        "raw_cases": 0,
        "imported": 0,
        "filtered": 0,
        "filter_reasons": {},
        "per_file_cap": args.per_file_cap,
    }
    per_file_counts: dict[str, int] = {}

    for rel in rel_files:
        dest = VENDOR_DIR / rel.replace("/", "__")
        have_file = dest.exists() and dest.stat().st_size > 0
        if not have_file:
            if args.skip_download:
                continue
            if not download_file(rel, dest, args.mirror):
                continue
        downloaded.append(rel)
        stats["files"] += 1
        stem = dest.stem.replace(".", "_")
        for case in iter_cases_from_file(dest, stem):
            stats["raw_cases"] += 1
            if args.per_file_cap > 0 and per_file_counts.get(stem, 0) >= args.per_file_cap:
                stats["filtered"] += 1
                stats["filter_reasons"]["per_file_cap"] = (
                    stats["filter_reasons"].get("per_file_cap", 0) + 1
                )
                continue
            ok, reason = case_is_in_scope(case)
            if not ok:
                stats["filtered"] += 1
                stats["filter_reasons"][reason] = (
                    stats["filter_reasons"].get(reason, 0) + 1
                )
                continue
            chunks.append(case_to_test_lines(case))
            stats["imported"] += 1
            per_file_counts[stem] = per_file_counts.get(stem, 0) + 1
            if stats["imported"] >= args.max_cases:
                break
        if stats["imported"] >= args.max_cases:
            break

    if stats["imported"] == 0:
        print("ERROR: no cases imported; check network/mirror or vendor cache", file=sys.stderr)
        sys.exit(1)

    OUT_FILE.write_text("".join(chunks), encoding="utf-8")
    MANIFEST.write_text("\n".join(downloaded) + "\n", encoding="utf-8")
    FILTER_STATS.write_text(json.dumps(stats, indent=2), encoding="utf-8")
    print(f"Official sqllogic files used: {stats['files']}")
    print(f"Raw cases scanned: {stats['raw_cases']}")
    print(f"Filtered out: {stats['filtered']} {stats['filter_reasons']}")
    print(f"Wrote {stats['imported']} cases to {OUT_FILE}")
    print(f"Filter stats: {FILTER_STATS}")


if __name__ == "__main__":
    main()
