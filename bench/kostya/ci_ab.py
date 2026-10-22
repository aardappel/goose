#!/usr/bin/env python3
"""A/B of two Goose compilers on the LangArena suite, in time and memory, for CI.

  python bench/kostya/ci_ab.py --base-goose A/build/goose --head-goose B/build/goose \
      [--base-src A/bench/kostya/goose/src] [--head-src B/bench/kostya/goose/src] \
      [--reps 5] [--summary out.md] [--json out.json]

Both compilers build the head's benchmark sources, so the two binaries differ
only in the compiler and its standard library; if the base compiler cannot
compile them (the head changed the language and the benchmarks with it), it
builds its own sources instead and the report says so.

Every benchmark runs in a process of its own (`benchmark run.js <name>`), base
and head back to back with the order swapped every rep, so a drift of the
machine hits both alike. Time is the benchmark's own measurement, peak memory
the process's maximum resident set size (wait4). A benchmark is a regression
when the head's best is more than --time-threshold (--mem-threshold) worse
than the base's best and no head rep was as good as the worst base rep: a
shared CI runner moves single runs by more than any threshold worth setting,
so a difference has to hold across every rep to count. The geometric mean of
the time ratios has a tighter threshold of its own (--geomean-threshold).

The exit status is 1 on a wrong checksum from the head, 2 on a regression
and no wrong checksum, 0 otherwise.
"""

import argparse
import json
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare

MiB = 1024


def build(tag, goose, srcs):
    """The benchmark binary `goose` makes of the first of `srcs` it compiles,
    and which one that was."""
    for src in srcs:
        try:
            return compare.build_goose(tag=tag, compiler=goose, src=src), src
        except SystemExit:
            print(f"{tag}: {goose} failed on {src}", file=sys.stderr)
    sys.exit(f"{tag}: no benchmark sources compile")


def run_one(exe, config, name):
    """(ok, seconds, peak RSS in KiB) of one benchmark in a process of its own."""
    with tempfile.TemporaryFile("w+", errors="replace") as out:
        p = subprocess.Popen([str(exe), str(config), name], cwd=compare.ARENA,
                             stdout=out, stderr=subprocess.STDOUT)
        _, status, ru = os.wait4(p.pid, 0)
        p.returncode = os.waitstatus_to_exitcode(status)
        out.seek(0)
        text = out.read()
    for line in text.splitlines():
        m = compare.LINE.match(line.strip())
        if m and m.group(1) == name:
            return m.group(2) == "OK", float(m.group(3)), ru.ru_maxrss
    print(f"{exe} {name}: no result (exit {p.returncode})\n{text[-2000:]}", file=sys.stderr)
    return False, math.nan, ru.ru_maxrss


def verdict(base, head, threshold, floor=0.0):
    """+1 for a regression of head over base, -1 for an improvement, 0 otherwise.
    `base` and `head` are the samples, lower is better; `floor` is the smallest
    absolute difference that counts."""
    if not base or not head:
        return 0
    b, h = min(base), min(head)
    if h > b * (1 + threshold) and h - b > floor and min(head) > max(base):
        return 1
    if b > h * (1 + threshold) and b - h > floor and min(base) > max(head):
        return -1
    return 0


def mark(v):
    return {1: " :red_circle:", -1: " :green_circle:"}.get(v, "")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base-goose", required=True)
    ap.add_argument("--head-goose", required=True)
    ap.add_argument("--base-src", help="the base's benchmark sources, if the head's do not compile")
    ap.add_argument("--head-src", default=str(compare.GOOSE_SRC / "src"))
    ap.add_argument("--base-label", default="base")
    ap.add_argument("--head-label", default="head")
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--only", help="comma-separated benchmark names")
    ap.add_argument("--config", default=str(compare.ARENA / "run.js"))
    ap.add_argument("--time-threshold", type=float, default=0.10)
    ap.add_argument("--geomean-threshold", type=float, default=0.03)
    ap.add_argument("--mem-threshold", type=float, default=0.10)
    ap.add_argument("--mem-floor", type=float, default=1.0, help="MiB a memory change must exceed")
    ap.add_argument("--summary", help="write the Markdown report here too")
    ap.add_argument("--json", help="write the raw samples here")
    args = ap.parse_args()

    compare.BUILD.mkdir(parents=True, exist_ok=True)
    head_exe, _ = build("ab-head", args.head_goose, [args.head_src])
    base_srcs = [args.head_src] + ([args.base_src] if args.base_src else [])
    base_exe, base_src = build("ab-base", args.base_goose, base_srcs)

    names = [e["name"] for e in json.loads(Path(args.config).read_text())]
    if args.only:
        names = [n for n in names if n in args.only.split(",")]
    sides = {"base": base_exe, "head": head_exe}
    samples = {s: {n: {"t": [], "rss": [], "ok": True} for n in names} for s in sides}
    for rep in range(args.reps):
        order = ["base", "head"] if rep % 2 == 0 else ["head", "base"]
        for n in names:
            for s in order:
                ok, t, rss = run_one(sides[s], args.config, n)
                cell = samples[s][n]
                cell["ok"] &= ok
                if not math.isnan(t):
                    cell["t"].append(t)
                cell["rss"].append(rss)
        print(f"rep {rep + 1}/{args.reps} done", file=sys.stderr, flush=True)

    rows, logs = [], []
    regressions, improvements, failures = [], [], []
    for n in names:
        b, h = samples["base"][n], samples["head"][n]
        if not h["ok"]:
            failures.append(n)
        vt = verdict(b["t"], h["t"], args.time_threshold)
        vm = verdict(b["rss"], h["rss"], args.mem_threshold, args.mem_floor * MiB)
        tb, th = (min(x["t"]) if x["t"] else math.nan for x in (b, h))
        mb, mh = min(b["rss"]) / MiB, min(h["rss"]) / MiB
        if tb > 0 and th > 0:
            logs.append(math.log(th / tb))
        for v, what in ((vt, "time"), (vm, "memory")):
            (regressions if v > 0 else improvements if v < 0 else []).append(f"{n} ({what})")
        rows.append(f"| {n}{'' if h['ok'] else ' :x:'} | {tb:.3f} | {th:.3f} | {th / tb - 1:+.1%}{mark(vt)} "
                    f"| {mb:.1f} | {mh:.1f} | {mh / mb - 1:+.1%}{mark(vm)} |")
    geo = math.exp(sum(logs) / len(logs)) if logs else 1.0
    geo_bad = geo > 1 + args.geomean_threshold
    if geo_bad:
        regressions.append(f"geomean time {geo - 1:+.1%}")

    out = ["## LangArena A/B", ""]
    if failures:
        out.append(f":x: **Wrong checksum or no result from the head:** {', '.join(failures)}")
    if regressions:
        out.append(f":red_circle: **Regressions:** {', '.join(regressions)}")
    if improvements:
        out.append(f":green_circle: Improvements: {', '.join(improvements)}")
    if not (failures or regressions or improvements):
        out.append("No significant change in time or memory.")
    out += ["",
            f"Geomean of time ratios (head/base): **{geo:.3f}** ({geo - 1:+.1%}){mark(1 if geo_bad else 0)}",
            "",
            f"Base: {args.base_label} · head: {args.head_label} · best of {args.reps} interleaved reps, "
            f"one process per benchmark. A change is marked when it exceeds {args.time_threshold:.0%} "
            f"(time) or {args.mem_threshold:.0%} and {args.mem_floor:g} MiB (peak RSS) and holds in every rep.",
            ]
    if base_src != args.head_src:
        out.append(f"\nThe base compiler could not compile the head's benchmark sources and built its own ({base_src}).")
    out += ["", "<details><summary>All benchmarks</summary>", "",
            "| benchmark | base s | head s | Δ time | base MiB | head MiB | Δ memory |",
            "|---|---:|---:|---:|---:|---:|---:|", *rows, "", "</details>"]
    report = "\n".join(out) + "\n"
    print(report)
    if args.summary:
        Path(args.summary).write_text(report)
    if args.json:
        Path(args.json).write_text(json.dumps(samples, indent=1))
    sys.exit(1 if failures else 2 if regressions else 0)


if __name__ == "__main__":
    main()
