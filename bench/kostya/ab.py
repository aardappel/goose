#!/usr/bin/env python3
"""Interleaved A/B timing of several builds of the Goose LangArena suite.

  python bench/kostya/ab.py [--reps N] [--copies K] [--only A,B] exe1 exe2 ...

Every rep runs every build once, in turn, so slow drifts of the machine hit
all builds alike, and the best time per benchmark is kept. On Windows the
address an executable is loaded at depends on its file name (ASLR picks an
image base per file), and that alone moved some benchmarks by tens of
percent, so each build is also run as K renamed copies and the best over all
of them is reported. Runs are pinned like compare.py's.
"""

import argparse
import math
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exes", nargs="+")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--copies", type=int, default=3)
    ap.add_argument("--only")
    ap.add_argument("--cpus", default="16-31")
    ap.add_argument("--config", default=str(compare.ARENA / "run.js"))
    args = ap.parse_args()
    compare.pin_cpus(args.cpus)
    only = args.only.split(",") if args.only else None
    builds = []
    for k, e in enumerate(args.exes):
        src = Path(e).resolve()
        copies = []
        for c in range(args.copies):
            dst = compare.BUILD / "ab" / f"b{k}" / f"{src.stem}_{c}{src.suffix}"
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
            copies.append(dst)
        builds.append(copies)
    best = [dict() for _ in builds]
    for rep in range(args.reps):
        for c in range(args.copies):
            for k, copies in enumerate(builds):
                res = compare.run_suite("goose", args.config, only, copies[c])
                for name, (ok, t, line) in res.items():
                    if not ok:
                        print(f"FAIL v{k}: {line}")
                    best[k][name] = min(best[k].get(name, math.inf), t)
        print(f"rep {rep + 1}/{args.reps} done", file=sys.stderr)
    names = list(best[0])
    print("| benchmark | " + " | ".join(f"v{k}" for k in range(len(builds))) + " | " +
          " | ".join(f"v0/v{k}" for k in range(1, len(builds))) + " |")
    print("|" + "---|" * (1 + 2 * len(builds) - 1))
    logs = [[] for _ in builds]
    for n in names:
        ts = [b.get(n) for b in best]
        for k in range(1, len(builds)):
            logs[k].append(math.log(ts[0] / ts[k]))
        print(f"| {n} | " + " | ".join(f"{t:.3f}" for t in ts) + " | " +
              " | ".join(f"{ts[0] / t:.2f}" for t in ts[1:]) + " |")
    print("| **total / geomean** | " + " | ".join(f"{sum(b.values()):.2f}" for b in best) + " | " +
          " | ".join(f"{math.exp(sum(l) / len(l)):.3f}" for l in logs[1:]) + " |")
    for k, e in enumerate(args.exes):
        print(f"v{k} = {e}")


if __name__ == "__main__":
    main()
