#!/usr/bin/env python3
"""Differential fuzzing of bounds-check elimination: random programs built
from the loop shapes the pass proves (two indices walking towards each
other, lookaheads, branch-free partitions, min/max windows, `i + c <= len`
steps, chunks of `len / k`, `a..a + n` windows, scanners slicing
`start..pos`, ranges from `i + 1`, inclusive range ends, searches returning
an index or -1, doubling merge passes, helpers with early returns, push
loops a `continue` in a block may cut short, cursors moved before a break,
if-values as indices), each with random off-by-one
mutations that make some of them unsafe. Every program runs through the JIT
with the pass on and off; a check the pass wrongly elides turns an abort into
a read past the end, so the two runs differ. Programs that differ are kept.

  python bench/bce_fuzz.py [--exe build/Release/goose.exe] [--seed 1] [--count 200]
"""

import argparse
import random
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def shape(r, a, n):
    """One loop shape over array `a` (lines of Goose); n makes names unique."""
    def pick(*xs):
        return r.choice(xs)
    i, j, lt, s, e, w = f"i{n}", f"j{n}", f"lt{n}", f"s{n}", f"e{n}", f"w{n}"
    off = pick(0, 0, 0, 1, -1)
    off2 = pick(0, 0, 0, 1, -1, 2)
    k = r.randint(0, 4)
    kind = r.randint(0, 15)
    if kind == 0:   # two indices towards each other
        return [f"var {i} = {max(0, off)};", f"var {j} = {a}.len - {1 - off2 if off2 <= 1 else 0};",
                f"while {i} {pick('<', '<', '<=')} {j} {{",
                f"    acc += {a}[{i}] * 3 + {a}[{j}];", f"    {i}++;", f"    {j}--;", "}"]
    if kind == 1:   # lookahead
        return [f"var {i} = 0;", f"while {i} < {a}.len {{",
                f"    if {a}[{i}] > 4 && {i} + {1 + max(0, off)} {pick('<', '<', '<=')} {a}.len"
                f" && {a}[{i} + 1] > 2 {{ acc += {a}[{i} + 1]; {i} += 2; continue; }}",
                f"    {i}++;", "}"]
    if kind == 2:   # branch-free partition
        return [f"var {lt} = {max(0, off)};", f"for {i} in {a}.len{' - 1' if off2 == 1 else ''} {{",
                f"    let x{n} = {a}[{i}];", f"    acc += {a}[{lt}];",
                f"    {lt} += if x{n} < {k + 2} {{ 1 }} else {{ {pick(0, 0, 1)} }};", "}",
                f"if {lt} < {a}.len {{ acc += {a}[{lt}]; }}"]
    if kind == 3:   # min/max window
        return [f"for {i} in {a}.len {{", f"    let {s} = max({i} - {k}, {pick(0, 0, -1)});",
                f"    let {e} = min({i} + {k} + 1, {a}.len{' + 1' if off == 1 else ''});",
                f"    for {j} in {s}..{e} {{ acc += {a}[{j}]; }}", "}"]
    if kind == 4:   # i + c <= len steps
        return [f"var {i} = 0;", f"while {i} + {3 + off} <= {a}.len {{",
                f"    acc += {a}[{i}] + {a}[{i} + 1] * 2 + {a}[{i} + 2] * 4;",
                f"    {i} += {pick(3, 3, 2, 1)};", "}"]
    if kind == 5:   # chunks of len / c
        c = pick(2, 3, 4)
        return [f"for {i} in {a}.len / {c + (1 if off == -1 else 0)} {{",
                f"    for {j} in {c + (1 if off == 1 else 0)} {{ acc += {a}[{i} * {c} + {j}]; }}", "}"]
    if kind == 6:   # window a..a + n
        return [f"if {k} <= {a}.len {{", f"    let {w} = {a}[{pick(0, 0, 1)}..{pick(0, 0, 1)} + {k}];",
                f"    for {j} in {k + off} {{ acc += {w}[{j}]; }}", "}"]
    if kind == 7:   # scanner slicing start..pos
        return [f"var {i} = 0;", f"while {i} < {a}.len {{", f"    let {s} = {i};",
                f"    while {i} < {a}.len && {a}[{i}] != {k} {{ {i}++; }}",
                f"    acc += {a}[{s}..{i}{' + 1' if off == 1 else ''}].len;",
                f"    if {i} < {a}.len {{ {i}++; }}", "}"]
    if kind == 8:   # range from i + 1
        return [f"for {i} in {a}.len {{",
                f"    for {j} in {i} + {1 + off}..{a}.len {{ acc += {a}[{j}] * {a}[{i}]; }}", "}"]
    if kind == 9:   # inclusive range end
        return [f"if {a}.len > 0 {{", f"    let {e} = {a}.len - {1 - max(-1, min(off, 1))};",
                f"    for {j} in {pick(0, 1)}..{e} + 1 {{ acc += {a}[{j}]; }}", "}"]
    if kind == 10:  # search (often for the last element), then index
        v = pick(str(k), f"{a}[{a}.len - 1]")
        return [f"if {a}.len > 0 {{", f"    let {e} = find({a}, {v});",
                f"    if {e} >= {pick(0, 0, -1)} {{ acc += {a}[{e}{' + 1' if off == 1 else ''}]; }}", "}"]
    if kind == 12:  # a helper with early returns
        return [f"if {a}.len > 0 {{",
                f"    acc += {a}[clampi({k} + {off}, {pick(0, 0, -1)}, {a}.len - 1 + {max(0, off2)})];", "}"]
    if kind == 13:  # a push loop whose iterations a continue in a block may skip
        return [f"var q{n}: i64[>..] = [];",
                f"for {i} in {a}.len {{", f"    block {{ if {a}[{i}] > {k + 2} {{ continue; }} }}",
                f"    q{n}.push({a}[{i}]);", "}",
                f"if {a}.len > 0 {{ acc += q{n}[{pick(f'q{n}.len - 1', f'{a}.len - 1', f'q{n}.len - 1')}]; }}"]
    if kind == 14:  # a loop left through a break that moved the cursor
        return [f"var {i} = 0;", "loop {",
                f"    if {i} >= {a}.len {{ {i} += {max(0, off)}; break; }}", f"    {i}++;", "}",
                f"if {i} > 0 {{ acc += {a}[{i} - 1]; }}"]
    if kind == 15:  # an if's value as the index
        return [f"if {a}.len > 0 {{",
                f"    let {e} = if {a}[0] > {k} {{ {a}.len - 1 }} else {{ {max(0, off)} + {pick(0, 0, 1)} }};",
                f"    acc += {a}[{e}];", "}"]
    return [f"var {w} = 1;", f"while {w} < {a}.len {{", f"    var {i} = 0;",   # doubling passes
            f"    while {i} < {a}.len {{", f"        let {s} = min({i} + {w}, {a}.len);",
            f"        if {s} > {i} {{ acc += {a}[{s} - 1]; }}",
            f"        acc += {a}[min({i} + {w}{' + 1' if off == 1 else ''}, {a}.len - 1)];",
            f"        {i} += 2 * {w};", "    }", f"    {w} *= 2;", "}"]


def program(seed):
    r = random.Random(seed)
    lines = ["import std;",
             "fn find(xs: i64[:], v: i64) -> i64 {", "    for x, i in xs { if x == v { return i; } }",
             "    return -1;", "}",
             "fn clampi(x: i64, lo: i64, hi: i64) -> i64 {",
             "    if x < lo { return lo; }", "    if x > hi { return hi; }", "    return x;", "}",
             "fn body(a: i64[:], b: i64[:]) -> i64 {", "    var acc = 0;"]
    for n in range(r.randint(2, 5)):
        lines += ["    " + l for l in shape(r, r.choice(["a", "b"]), n)]
    lines += ["    return acc;", "}", "fn main() {",
              f"    var xs: i64[>..] = [{', '.join(str(r.randint(0, 9)) for _ in range(12))}];",
              f"    var ks: i64[>..] = [{r.randint(0, 12)}, {r.randint(0, 12)}];",
              "    print(body(xs[..ks[0]], xs[..ks[1]]));", "}"]
    return "\n".join(lines) + "\n"


def run(exe, path, nobce):
    argv = [exe, "-O2", "--jit"] + (["--no-bce"] if nobce else []) + [str(path)]
    try:
        p = subprocess.run(argv, capture_output=True, text=True, errors="replace", timeout=60)
    except subprocess.TimeoutExpired:
        return None
    return p.returncode, p.stdout, [l for l in p.stderr.splitlines() if "runtime error" in l]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=str(ROOT / "build" / "Release" / "goose.exe"))
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--count", type=int, default=200)
    args = ap.parse_args()
    args.exe = str(Path(args.exe).resolve())
    out = ROOT / "build" / "gen" / "bce_fuzz"
    out.mkdir(parents=True, exist_ok=True)
    ran = aborted = bad = 0
    for seed in range(args.seed, args.seed + args.count):
        path = out / f"f{seed}.goose"
        path.write_text(program(seed), encoding="utf-8", newline="\n")
        on, off = run(args.exe, path, False), run(args.exe, path, True)
        if on is None or off is None:
            continue
        ran += 1
        aborted += off[0] != 0
        if on != off:
            bad += 1
            print(f"MISMATCH {path}: with the pass {on}, without {off}")
        else:
            path.unlink()
    print(f"bce_fuzz: {ran} programs, {aborted} of them aborting, {bad} mismatches")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
