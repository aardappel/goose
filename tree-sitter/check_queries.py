#!/usr/bin/env python3
"""Compile the Helix queries and check their captures on a syntax fixture.

Run through `python tree-sitter/build.py --check`. This checks tree-sitter
matches, not Helix's rendering, indentation algorithm or capture precedence.
"""

import json
import re
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
CAPTURE = re.compile(
    r"capture: (?:\d+ - )?([\w.]+), "
    r"start: \((\d+), (\d+)\), end: \((\d+), (\d+)\)"
)


def check_queries(ts):
    fixture = HERE / "test" / "helix" / "queries.goose"
    source = fixture.read_bytes()
    lines = source.splitlines(keepends=True)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))
    expected = json.loads(fixture.with_suffix(".json").read_text(encoding="utf-8"))
    queries = sorted((HERE / "queries" / "helix").glob("*.scm"))
    if {p.stem for p in queries} != set(expected):
        raise ValueError("every Helix query must have capture expectations")
    count = 0
    for query in queries:
        result = subprocess.run(
            [ts, "query", str(query), str(fixture)], cwd=HERE,
            capture_output=True, text=True, encoding="utf-8", check=True,
        )
        captures = set()
        for match in CAPTURE.finditer(result.stdout):
            name, start_row, start_col, end_row, end_col = match.groups()
            start = offsets[int(start_row)] + int(start_col)
            end = offsets[int(end_row)] + int(end_col)
            captures.add((name, source[start:end].decode("utf-8")))
        for name, texts in expected[query.stem]["captures"].items():
            for text in texts:
                if (name, text) not in captures:
                    raise ValueError(f"{query.name}: missing @{name} for {text!r}")
                count += 1
        for name, texts in expected[query.stem].get("absent", {}).items():
            for text in texts:
                if (name, text) in captures:
                    raise ValueError(f"{query.name}: unexpected @{name} for {text!r}")
                count += 1
    print(f"Helix queries: {len(queries)} compiled, {count} capture checks passed")
