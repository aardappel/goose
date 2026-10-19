#!/usr/bin/env python3
"""Generate the tree-sitter parser from grammar.js, and optionally check it.

    python tree-sitter/build.py [--check]

Writes src/parser.c, src/grammar.json, src/node-types.json and
src/tree_sitter/ next to this script, where an editor building the grammar
from this directory looks for them; src/scanner.c is written by hand and
left alone. The tree-sitter CLI is the version package.json pins, installed
into node_modules/ by npm on first use, so Node.js is the one requirement.

With --check, the corpus under test/corpus is run (tree-sitter test), and
every .goose file in the repository is parsed: each must parse without an
error, except the ones malformed on purpose (MALFORMED). A FAIL line names
each file that did not, and the exit status is nonzero where any failed.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# Sources written to exercise the compiler's error reporting and recovery,
# which a parse is expected to reject: directories (ending in /) or files,
# relative to the repository root.
MALFORMED = [
    "test/errors/",
    "test/errors_tc/recovery/",
    "test/lsp/broken_decls.goose",
    "test/lsp/complete.goose",
    "test/lsp/exemplar.goose",
    "test/syntax/lexer_tokens.goose",
]


def cli():
    """The pinned tree-sitter CLI, installing it on first use."""
    exe = HERE / "node_modules" / ".bin" / ("tree-sitter.cmd" if os.name == "nt" else "tree-sitter")
    if not exe.exists():
        npm = shutil.which("npm")
        if not npm:
            sys.exit("npm not found: install Node.js to generate the parser")
        subprocess.run([npm, "install", "--no-audit", "--no-fund"], cwd=HERE, check=True)
    return str(exe)


def malformed(rel):
    return any(rel.startswith(m) if m.endswith("/") else rel == m for m in MALFORMED)


def parse_repository(ts):
    """Parses every .goose file; returns the ones that should have parsed but did not."""
    files = sorted(p for p in ROOT.rglob("*.goose")
                   if not any(part.startswith(".") or part.startswith("build") or part == "node_modules"
                              for part in p.relative_to(ROOT).parts))
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join(str(p) for p in files))
        listing = f.name
    try:
        # Exits nonzero when any file fails; the failures are read from the output.
        r = subprocess.run([ts, "parse", "--quiet", "--paths", listing], cwd=HERE,
                           capture_output=True, text=True, errors="replace")
    finally:
        os.unlink(listing)
    failed = {Path(line.split("\t")[0].strip()).resolve() for line in r.stdout.splitlines() if "\t" in line}
    rels = [p.relative_to(ROOT).as_posix() for p in failed]
    print(f"parsed {len(files)} files")
    return sorted(rel for rel in rels if not malformed(rel))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true",
                    help="run the corpus and parse every .goose file in the repository")
    args = ap.parse_args()

    ts = cli()
    subprocess.run([ts, "generate"], cwd=HERE, check=True)
    if not args.check:
        return 0

    ok = subprocess.run([ts, "test"], cwd=HERE).returncode == 0
    for rel in parse_repository(ts):
        print(f"FAIL {rel}")
        ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
