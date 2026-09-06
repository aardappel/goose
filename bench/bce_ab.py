#!/usr/bin/env python3
"""What bounds-check elimination is worth, measured directly: the same Goose
sources built with and without `--no-bce`, under every available toolchain, at
the size baked into each source file. The compiler's own elision counts are
printed alongside, so a benchmark where nothing was elided is visible as such.

  python bench/bce_ab.py                        # the benchmarks that index at all
  python bench/bce_ab.py --names graph,words    # a subset

Benchmarks with no index or slice expressions (sum, push, tree, interp,
particles) are omitted: there is nothing for the pass to do in them.
"""

import argparse
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
import toolchain as tc

HERE = Path(__file__).resolve().parent

DEFAULT_NAMES = ["graph", "graph_csr", "words", "strlist", "sexp", "records_var",
                 "lru", "scene", "calc", "respond", "blur", "blur_assert",
                 "blur_rows"]

# See run_bench.py: the large sizes need more than the runtime's default
# reservation, and this is the largest that keeps u32 relative references
# storable without a range check.
STACK_RESERVE = "GS_STACK_RESERVE=2147483648ull"


def measure(exe, reps):
    """Best of `reps`, after two discarded warm-ups (see notes.md on why)."""
    for _ in range(2):
        tc.run_measured(exe)
    best, out = None, ""
    for _ in range(reps):
        r = tc.run_measured(exe)
        if best is None or r.ms < best:
            best = r.ms
        out = r.out
    return best, out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--names", default=",".join(DEFAULT_NAMES),
                    help="comma-separated benchmarks to measure")
    ap.add_argument("--reps", type=int, default=4)
    ap.add_argument("--exe", help="the goose compiler to use")
    ap.add_argument("--dir", default=str(HERE / "gen" / "bce"),
                    help="where the generated sources and binaries go")
    args = ap.parse_args()

    tc.setup_console()
    goose = tc.find_goose(args.exe)
    ccs = tc.find_ccs()
    if not ccs:
        sys.exit("no C toolchain found")
    gendir = Path(args.dir)
    gendir.mkdir(parents=True, exist_ok=True)
    shutil.copy(HERE / "goose" / "rng.goose", gendir)

    rows = []
    for n in args.names.split(","):
        shutil.copy(HERE / "goose" / f"{n}.goose", gendir)
        row = {"name": n, "stat": ""}
        for mode in ("bce", "nobce"):
            cfile = gendir / f"{n}-{mode}.c"
            cfile.unlink(missing_ok=True)
            gargs = ["-O2"] + (["--no-bce"] if mode == "nobce" else [])
            gargs += ["-o", str(cfile), str(gendir / f"{n}.goose")]
            code, out, err = tc.run_capture([goose] + gargs)
            if not cfile.exists():
                print(f"goose failed on {n} {mode}\n{out}{err}")
                continue
            m = re.search(r"bce: (.*)", out + err)
            if mode == "bce" and m:
                row["stat"] = m.group(1).strip()
            for tcname, cc in ccs.items():
                binary = gendir / f"{n}-{mode}-{tcname}{tc.EXE_SUFFIX}"
                binary.unlink(missing_ok=True)
                cc.compile(cfile, binary, opt=2, defines=[STACK_RESERVE],
                           log=gendir / f"{n}-{mode}-{tcname}.cc.log")
                if not binary.exists():
                    print(f"{tcname} failed on {n} {mode}")
                    continue
                ms, out = measure(binary, args.reps)
                row[f"{mode}-{tcname}"] = ms
                row[f"out-{mode}"] = out
        if row.get("out-bce") and row.get("out-nobce") and row["out-bce"] != row["out-nobce"]:
            print(f"!! {n} : output differs with and without BCE -- the pass is unsound")
        rows.append(row)

    def gain(on, off):
        return (off / on - 1) * 100 if on and off else 0.0

    def ms(v):
        return tc.num(v) if v else "--"

    names = list(ccs)
    print()
    print(f"{'benchmark':<12} {'elided':<34}" + "  ".join(
        f" {tcname + ' on':>9} {tcname + ' off':>9} {'gain':>7}" for tcname in names))
    for r in rows:
        cells = []
        for tcname in names:
            on, off = r.get(f"bce-{tcname}"), r.get(f"nobce-{tcname}")
            cells.append(f" {ms(on):>9} {ms(off):>9} {tc.num(gain(on, off)):>6}%")
        print(f"{r['name']:<12} {r['stat']:<34}" + "  ".join(cells))
    return 0


if __name__ == "__main__":
    sys.exit(main())
