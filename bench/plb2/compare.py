#!/usr/bin/env python3
"""Build and run the plb2 benchmarks in Goose, C and Rust.

The Goose implementations live in `goose/` here. The C and Rust ones are read
from a checkout of https://github.com/attractivechaos/plb2 (by default the
sibling of this repository's checkout, `../plb2`, or `$PLB2`), so nothing is
copied out of it and nothing is written into it: every build product goes
under `bench/plb2/build/`.

Every language is built the way plb2's own Makefiles build it: C with clang
`-O3 -fwrapv -flto`, Rust with `rustc -C opt-level=3`. The generated Goose C
is compiled by the same clang at `-O3`. On Windows C and Goose both get
`-fstrict-aliasing`, which is clang's default everywhere but the MSVC target.

  python bench/plb2/compare.py build                  # all three languages
  python bench/plb2/compare.py build goose            # one of them
  python bench/plb2/compare.py build c --tag c-native --cflags=-march=native
  python bench/plb2/compare.py build goose --tag goose-nosimd --no-simd --only matmul
  python bench/plb2/compare.py run                    # timings and a ratio table
  python bench/plb2/compare.py run --only matmul --langs goose,goose-nosimd,c,c-native
  python bench/plb2/compare.py report                 # table from saved results

A run checks every output against C's first and times the rest with output
to the null device, as plb2's hyperfine runs do. Runs interleave the
languages, rep by rep, so a slow stretch of the machine hits all of them,
and each executable runs as several renamed copies (`--copies`). The table
shows the best time of each.
"""

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BUILD = HERE / "build"
GOOSE_SRC = HERE / "goose"
IS_WINDOWS = os.name == "nt"
# What a program's runtime links besides libm and threads: dlopen, in libdl on
# Linux before glibc 2.34.
GOOSE_LIBS = ["-ldl"] if sys.platform.startswith("linux") else []
EXE = ".exe" if IS_WINDOWS else ""

BENCHES = ["nqueen", "matmul", "sudoku", "bedcov"]
LANGS = ["goose", "c", "rust"]

# Known differences from C's output that are not Goose's to fix: plb2's Rust
# bedcov generates n - 1 intervals per set rather than n, and its matmul
# prints the full float where C prints six decimals.
KNOWN_DIFF = {("rust", "bedcov"), ("rust", "matmul")}


def find_plb2():
    env = os.environ.get("PLB2")
    if env:
        return Path(env).resolve()
    # A worktree lives under <checkout>/.claude/worktrees/<name>; plb2 is a
    # sibling of the checkout, not of the worktree.
    for base in [REPO] + list(REPO.parents):
        cand = base.parent / "plb2"
        if (cand / "src" / "c" / "nqueen.c").exists():
            return cand
    sys.exit("plb2 checkout not found: clone https://github.com/attractivechaos/plb2 next to this "
             "repository's checkout, or set PLB2")


def vs_llvm_bin():
    if not IS_WINDOWS:
        return None
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
        "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return None
    r = subprocess.run([str(vswhere), "-latest", "-prerelease", "-property", "installationPath"],
                       capture_output=True, text=True)
    root = Path(r.stdout.strip().splitlines()[0])
    p = root / "VC" / "Tools" / "Llvm" / "x64" / "bin"
    return p if p.exists() else None


def clang():
    b = vs_llvm_bin()
    if b:
        return str(b / "clang.exe")
    cc = shutil.which("clang")
    if not cc:
        sys.exit("clang not found")
    return cc


def goose_exe():
    for sub in ("Release", "", "RelWithDebInfo", "Debug"):
        p = REPO / "build" / sub / ("goose" + EXE)
        if p.exists():
            return p
    sys.exit("no goose binary under build/")


def run(argv, cwd=None, check=True):
    r = subprocess.run([str(a) for a in argv], cwd=cwd, capture_output=True, text=True,
                       errors="replace")
    if check and r.returncode != 0:
        sys.stderr.write(f"failed: {' '.join(str(a) for a in argv)}\n{r.stdout}{r.stderr}\n")
        sys.exit(1)
    return r


STRICT = ["-fstrict-aliasing"] if IS_WINDOWS else []
# plb2's src/c/Makefile; lld does the link-time optimization on Windows.
C_FLAGS = ["-O3", "-fwrapv", "-flto"] + STRICT + (["-fuse-ld=lld"] if IS_WINDOWS else [])
GOOSE_FLAGS = ["-O2"]
GOOSE_CFLAGS = ["-O3", "-w"] + STRICT
# plb2's src/rust/Makefile.
RUST_FLAGS = ["-C", "debuginfo=0", "-C", "opt-level=3"]


def exe_path(lang, bench, tag=None):
    return BUILD / (tag or lang) / (bench + EXE)


def build_goose(benches, tag="goose", extra_goose=(), extra_cflags=(), compiler=None, src=None,
                no_simd=False):
    cc = clang()
    out = BUILD / tag
    out.mkdir(parents=True, exist_ok=True)
    if no_simd:
        # The same sources with every `simd fn` an ordinary one: what the
        # baseline instruction set gives without the wider versions.
        plain = out / "src"
        plain.mkdir(exist_ok=True)
        for b in benches:
            text = (Path(src or GOOSE_SRC) / (b + ".goose")).read_text()
            (plain / (b + ".goose")).write_text(text.replace("simd fn ", "fn "))
        src = plain
    for b in benches:
        cfile = out / (b + ".c")
        run([compiler or goose_exe(), *GOOSE_FLAGS, *extra_goose, "--standalone", "-o", cfile,
             Path(src or GOOSE_SRC) / (b + ".goose")])
        run([cc, *GOOSE_CFLAGS, *extra_cflags, cfile, "-o", exe_path(tag, b)] +
            ([] if IS_WINDOWS else ["-lm", "-pthread"] + GOOSE_LIBS))
        print(f"{tag}: {exe_path(tag, b)}")


def build_c(benches, plb2, tag="c", extra_cflags=()):
    cc = clang()
    (BUILD / tag).mkdir(parents=True, exist_ok=True)
    for b in benches:
        run([cc, *C_FLAGS, *extra_cflags, plb2 / "src" / "c" / (b + ".c"), "-o", exe_path(tag, b)] +
            ([] if IS_WINDOWS else ["-lm"]))
        print(f"{tag}: {exe_path(tag, b)}")


def build_rust(benches, plb2, tag="rust", extra=()):
    (BUILD / tag).mkdir(parents=True, exist_ok=True)
    for b in benches:
        run(["rustc", *RUST_FLAGS, *extra, "-o", exe_path(tag, b), plb2 / "src" / "rust" / (b + ".rs")])
        print(f"{tag}: {exe_path(tag, b)}")


def pin_cpus(spec):
    """Restrict this process, and so every benchmark it starts, to the CPUs in
    `spec` ("16-31"). On a CPU with two core complexes of different cache
    sizes the times otherwise depend on where the scheduler puts a run."""
    cpus = set()
    for part in spec.split(","):
        lo, _, hi = part.partition("-")
        cpus.update(range(int(lo), int(hi or lo) + 1))
    if IS_WINDOWS:
        import ctypes
        mask = sum(1 << c for c in cpus)
        k32 = ctypes.windll.kernel32
        k32.GetCurrentProcess.restype = ctypes.c_void_p
        k32.SetProcessAffinityMask(ctypes.c_void_p(k32.GetCurrentProcess()), ctypes.c_size_t(mask))
    else:
        os.sched_setaffinity(0, cpus)


def db_path():
    return BUILD / "results.json"


def load_db():
    p = db_path()
    return json.loads(p.read_text()) if p.exists() else {}


def save_db(db):
    db_path().write_text(json.dumps(db, indent=1))


def timed(exe):
    t0 = time.perf_counter()
    r = subprocess.run([str(exe)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    t = time.perf_counter() - t0
    if r.returncode != 0:
        sys.stderr.write(f"{exe} exited {r.returncode}: {r.stderr.decode(errors='replace')[-500:]}\n")
        return None
    return t


def cmd_run(args):
    db = load_db()
    langs = args.langs.split(",") if args.langs else LANGS
    benches = args.only.split(",") if args.only else BENCHES
    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
    ok = {}
    for b in benches:
        ref = None
        cexe = exe_path("c", b)
        if cexe.exists():
            ref = subprocess.run([str(cexe)], capture_output=True).stdout
        for lang in langs:
            exe = exe_path(lang, b)
            if not exe.exists():
                sys.exit(f"{lang}: {exe} not built")
            out = subprocess.run([str(exe)], capture_output=True).stdout
            same = ref is None or out.replace(b"\r\n", b"\n") == ref.replace(b"\r\n", b"\n")
            ok[(lang, b)] = same
            if not same:
                lang_kind = lang.split(":")[-1].split("-")[0]
                note = " (known: plb2's own Rust differs)" if (lang_kind, b) in KNOWN_DIFF else ""
                print(f"{b}: {lang} output differs from C's{note}: "
                      f"{out[:60]!r}... vs {ref[:60]!r}...")
    # Windows places an executable by its file name, which alone can move a
    # benchmark by more than the differences measured here; renamed copies
    # spread that over every language alike.
    copies = {}
    for b in benches:
        for lang in langs:
            exe = exe_path(lang, b)
            copies[(lang, b)] = [exe]
            for c in range(1, args.copies):
                cp = exe.with_name(f"{exe.stem}_copy{c}{exe.suffix}")
                shutil.copyfile(exe, cp)
                copies[(lang, b)].append(cp)
    for rep in range(args.reps):
        for b in benches:
            for lang in langs:
                for exe in copies[(lang, b)]:
                    t = timed(exe)
                    if t is None:
                        continue
                    db.setdefault(lang, {}).setdefault(b, []).append({"t": t, "ok": ok[(lang, b)], "at": stamp})
                    print(f"[{rep + 1}/{args.reps}] {b} {lang}: {t:.3f}s")
    save_db(db)
    report(db, langs, benches, last=args.reps * args.copies)


def report(db, langs=None, benches=None, last=None):
    langs = langs or list(db)
    benches = benches or BENCHES

    def best(lang, b):
        rs = db.get(lang, {}).get(b, [])
        if last:
            rs = rs[-last:]
        ts = [r["t"] for r in rs]
        return min(ts) if ts else None

    goose = [l for l in langs if l.startswith("goose")]
    others = [l for l in langs if not l.startswith("goose")]
    hdr = "| benchmark | " + " | ".join(langs) + " | " + " | ".join(
        f"{o}/{g}" for g in goose for o in others) + " |"
    print(hdr)
    print("|" + "---|" * (hdr.count("|") - 1))
    logs = {(g, o): [] for g in goose for o in others}
    for b in benches:
        ts = {l: best(l, b) for l in langs}
        cells = [f"{ts[l]:.3f}" if ts[l] is not None else "-" for l in langs]
        ratios = []
        for g in goose:
            for o in others:
                if ts[g] and ts[o]:
                    r = ts[o] / ts[g]
                    logs[(g, o)].append(math.log(r))
                    ratios.append(f"{r:.2f}")
                else:
                    ratios.append("-")
        print(f"| {b} | " + " | ".join(cells) + " | " + " | ".join(ratios) + " |")
    if len(benches) > 1:
        geo = [f"{math.exp(sum(v) / len(v)):.2f}" if v else "-" for v in logs.values()]
        print("| **geomean** | " + " | ".join("" for _ in langs) + " | " + " | ".join(geo) + " |")
    print("(times are the best of the runs in seconds; a ratio above 1 means Goose is faster)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("langs", nargs="*")
    b.add_argument("--only", help="benchmarks to build, comma separated")
    b.add_argument("--tag", help="build directory under build/ (default: the language)")
    b.add_argument("--goose-flags", default="", help="extra goose flags, space separated")
    b.add_argument("--cflags", default="", help="extra clang flags (Goose and C), space separated")
    b.add_argument("--rustflags", default="", help="extra rustc flags, space separated")
    b.add_argument("--goose", help="the goose compiler to use instead of this checkout's build")
    b.add_argument("--src", help="the Goose sources to build instead of goose/")
    b.add_argument("--no-simd", action="store_true", help="build Goose with `simd fn` as plain `fn`")
    r = sub.add_parser("run")
    r.add_argument("--langs", help="languages (build tags) to run, comma separated")
    r.add_argument("--only", help="benchmarks to run, comma separated")
    r.add_argument("--reps", type=int, default=5)
    r.add_argument("--copies", type=int, default=3, help="renamed copies of each executable")
    rp = sub.add_parser("report")
    rp.add_argument("--langs")
    rp.add_argument("--only")
    rp.add_argument("--last", type=int, help="only the last N measurements per cell")
    ap.add_argument("--cpus", default=os.environ.get("BENCH_CPUS", "16-31"),
                    help="CPUs to pin runs to (default 16-31: on the Ryzen 9950X3D this was measured on, "
                         "the core complex without the stacked cache), or 'all'")
    args = ap.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    if args.cmd == "build":
        benches = args.only.split(",") if args.only else BENCHES
        langs = args.langs or LANGS
        plb2 = find_plb2() if any(l in ("c", "rust") for l in langs) else None
        for lang in langs:
            if lang == "goose":
                build_goose(benches, args.tag or "goose", args.goose_flags.split(), args.cflags.split(),
                            args.goose, args.src, args.no_simd)
            elif lang == "c":
                build_c(benches, plb2, args.tag or "c", args.cflags.split())
            elif lang == "rust":
                build_rust(benches, plb2, args.tag or "rust", args.rustflags.split())
            else:
                sys.exit(f"unknown language {lang}")
    elif args.cmd == "run":
        if args.cpus != "all":
            pin_cpus(args.cpus)
        cmd_run(args)
    else:
        report(load_db(), args.langs.split(",") if args.langs else None,
               args.only.split(",") if args.only else None, args.last)


if __name__ == "__main__":
    main()
