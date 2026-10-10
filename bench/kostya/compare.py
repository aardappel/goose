#!/usr/bin/env python3
"""Build and run the LangArena benchmarks locally in Goose, Rust and C++.

The Goose implementation lives in `goose/` here, laid out the way it would sit
in the LangArena repository. The Rust and C++ implementations are read from a
LangArena checkout (by default the sibling of this repository, `../LangArena`
next to the goose checkout), so nothing is copied out of it and nothing is
written into it: every build product goes under `bench/kostya/build/`.

C and C++ are compiled by clang, as LangArena compiles them (its `C++/Clang++`
row): the gcc-style `clang`/`clang++` drivers bundled with Visual Studio on
Windows, or the ones on PATH elsewhere. Rust is built with `cargo build
--release`, LangArena's `Rust` row.

  python bench/kostya/compare.py build                 # all three languages
  python bench/kostya/compare.py build goose           # one of them
  python bench/kostya/compare.py run                   # run.js, all languages, 3 reps
  python bench/kostya/compare.py run --langs goose,rust --only Sort,Json --reps 1
  python bench/kostya/compare.py test goose            # test.js: checksums only
  python bench/kostya/compare.py report                # table from saved results

Each run appends to `build/results.json`; `report` prints the best time per
benchmark and language, and the Goose ratio against each other language
(above 1.0 means Goose is faster).
"""

import argparse
import json
import os
import re
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
EXE = ".exe" if IS_WINDOWS else ""

LANGS = ["goose", "rust", "cpp"]


def find_arena():
    env = os.environ.get("LANGARENA")
    if env:
        return Path(env).resolve()
    # A worktree lives under <checkout>/.claude/worktrees/<name>; the arena is
    # a sibling of the checkout, not of the worktree.
    for base in [REPO] + list(REPO.parents):
        cand = base.parent / "LangArena"
        if (cand / "run.js").exists():
            return cand
    sys.exit("LangArena checkout not found: set LANGARENA")


ARENA = find_arena()


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


def clang_tools():
    b = vs_llvm_bin()
    if b:
        return str(b / "clang.exe"), str(b / "clang++.exe")
    cc, cxx = shutil.which("clang"), shutil.which("clang++")
    if not cc or not cxx:
        sys.exit("clang/clang++ not found")
    return cc, cxx


def goose_exe():
    for sub in ("Release", "", "RelWithDebInfo", "Debug"):
        p = REPO / "build" / sub / ("goose" + EXE)
        if p.exists():
            return p
    sys.exit("no goose binary under build/")


def run(argv, cwd=None, env=None, check=True):
    r = subprocess.run([str(a) for a in argv], cwd=cwd, env=env, capture_output=True, text=True,
                       errors="replace")
    if check and r.returncode != 0:
        sys.stderr.write(f"failed: {' '.join(str(a) for a in argv)}\n{r.stdout}{r.stderr}\n")
        sys.exit(1)
    return r


# --- Goose ---------------------------------------------------------------------

# Flags kept in one place so the LangArena `run` script and this file agree.
GOOSE_FLAGS = ["-O2"]
GOOSE_CFLAGS = ["-O3", "-w"] + (["-fstrict-aliasing"] if os.name == "nt" else [])


def build_goose(extra_goose=(), extra_cflags=(), tag="goose", entry="main.goose", compiler=None, src=None):
    cc, _ = clang_tools()
    out = BUILD / tag
    out.mkdir(parents=True, exist_ok=True)
    cfile = out / "benchmark.c"
    t0 = time.time()
    run([compiler or goose_exe(), *GOOSE_FLAGS, *extra_goose, "--standalone", "-o", cfile, Path(src or GOOSE_SRC / "src") / entry])
    t1 = time.time()
    exe = out / ("benchmark" + EXE)
    run([cc, *GOOSE_CFLAGS, *extra_cflags, cfile, "-o", exe] + ([] if IS_WINDOWS else ["-lm", "-pthread"]))
    print(f"goose: {exe} (goose {t1 - t0:.1f}s, clang {time.time() - t1:.1f}s)")
    return exe


# --- Rust ----------------------------------------------------------------------

def build_rust():
    env = dict(os.environ, CARGO_TARGET_DIR=str(BUILD / "rust-target"))
    t0 = time.time()
    run(["cargo", "build", "--release"], cwd=ARENA / "rust", env=env)
    exe = BUILD / "rust-target" / "release" / ("benchmarks" + EXE)
    print(f"rust: {exe} ({time.time() - t0:.1f}s)")
    return exe


# --- C++ -----------------------------------------------------------------------

DEPS = BUILD / "cpp-deps"
# LangArena's `make prod`, minus the Linux-only linker flags.
CXXFLAGS_PROD = ["-O2", "-DNDEBUG", "-fstack-protector-strong", "-fno-omit-frame-pointer", "-w"] + \
    (["-fstrict-aliasing"] if os.name == "nt" else [])
DEPS_CXXFLAGS_PROD = ["-O2", "-DNDEBUG", "-w"] + (["-fstrict-aliasing"] if os.name == "nt" else [])


def fetch_cpp_deps():
    """The same sources LangArena's cpp/Makefile downloads, plus re2 through
    vcpkg on Windows (elsewhere it is expected to be installed)."""
    DEPS.mkdir(parents=True, exist_ok=True)
    sj = DEPS / "simdjson"
    if not (sj / "simdjson.cpp").exists():
        sj.mkdir(exist_ok=True)
        for f in ("simdjson.h", "simdjson.cpp"):
            run(["curl", "-sSL", "-o", sj / f,
                 f"https://github.com/simdjson/simdjson/releases/download/v5.0.1/{f}"])
    if not (DEPS / "lazycsv.hpp").exists():
        run(["curl", "-sSL", "-o", DEPS / "lazycsv.hpp",
             "https://raw.githubusercontent.com/ashtum/lazycsv/refs/heads/master/include/lazycsv.hpp"])
    if not (DEPS / "base64").exists():
        run(["git", "clone", "-q", "https://github.com/aklomp/base64.git", DEPS / "base64"])


def re2_paths():
    if IS_WINDOWS:
        inst = DEPS / "re2pkg" / "vcpkg_installed" / "x64-windows-static"
        if not (inst / "include" / "re2" / "re2.h").exists():
            sys.exit("re2 missing: run vcpkg install --triplet x64-windows-static in build/cpp-deps/re2pkg")
        libs = sorted(str(p) for p in (inst / "lib").glob("*.lib"))
        return [f"-I{inst / 'include'}"], libs
    return [], ["-lre2"]


def build_cpp():
    fetch_cpp_deps()
    cc, cxx = clang_tools()
    out = BUILD / "cpp"
    objdir = out / "obj"
    depdir = out / "deps"
    objdir.mkdir(parents=True, exist_ok=True)
    depdir.mkdir(parents=True, exist_ok=True)
    re2_inc, re2_libs = re2_paths()
    t0 = time.time()

    # Dependencies, compiled once.
    b64 = DEPS / "base64"
    (b64 / "lib" / "config.h").write_text(
        "#define HAVE_AVX512 0\n#define HAVE_AVX2 1\n#define HAVE_NEON32 0\n#define HAVE_NEON64 0\n"
        "#define HAVE_SSSE3 1\n#define HAVE_SSE41 1\n#define HAVE_SSE42 1\n#define HAVE_AVX 1\n")
    codec_flags = {"avx2": ["-mavx2"], "ssse3": ["-mssse3"], "sse41": ["-msse4.1"],
                   "sse42": ["-msse4.2"], "avx": ["-mavx"], "generic": [], "avx512": [],
                   "neon32": [], "neon64": []}
    dep_objs = []
    jobs = []
    for arch, fl in codec_flags.items():
        src = b64 / "lib" / "arch" / arch / "codec.c"
        o = depdir / f"b64_{arch}.o"
        jobs.append(([cc, "-std=c99", *DEPS_CXXFLAGS_PROD, "-DBASE64_STATIC_DEFINE", *fl,
                      f"-I{b64 / 'lib'}", f"-I{b64 / 'include'}", "-c", src, "-o", o], o))
    for name in ("lib", "codec_choose", "tables/tables"):
        src = b64 / "lib" / f"{name}.c"
        o = depdir / f"b64_{name.replace('/', '_')}.o"
        jobs.append(([cc, "-std=c99", *DEPS_CXXFLAGS_PROD, "-DBASE64_STATIC_DEFINE",
                      f"-I{b64 / 'lib'}", f"-I{b64 / 'include'}", "-c", src, "-o", o], o))
    o = depdir / "simdjson.o"
    jobs.append(([cxx, "-std=c++20", *DEPS_CXXFLAGS_PROD, "-c", DEPS / "simdjson" / "simdjson.cpp",
                  "-o", o], o))

    inc = [f"-I{ARENA / 'cpp' / 'src'}", f"-I{DEPS}", f"-I{b64 / 'include'}", f"-I{DEPS / 'simdjson'}",
           *re2_inc, "-DBASE64_STATIC_DEFINE"] + (["-D_USE_MATH_DEFINES", "-include", "system_error"] if IS_WINDOWS else [])
    srcs = [ARENA / "cpp" / "main.cpp"] + sorted((ARENA / "cpp" / "src").glob("*.cpp"))
    for s in srcs:
        o = objdir / (s.stem + ".o")
        jobs.append(([cxx, "-std=c++20", *inc, *CXXFLAGS_PROD, "-c", s, "-o", o], o))

    procs = []
    for argv, o in jobs:
        if o.exists() and o.stat().st_mtime > max(Path(a).stat().st_mtime for a in argv
                                                   if isinstance(a, Path) and a.suffix in (".c", ".cpp")):
            dep_objs.append(o)
            continue
        procs.append((subprocess.Popen([str(a) for a in argv], stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, text=True, errors="replace"), argv, o))
        dep_objs.append(o)
    for p, argv, o in procs:
        outp = p.communicate()[0]
        if p.returncode != 0:
            sys.stderr.write(f"failed: {' '.join(map(str, argv))}\n{outp}\n")
            sys.exit(1)
    exe = out / ("benchmark" + EXE)
    run([cxx, *dep_objs, *re2_libs, "-o", exe] + ([] if IS_WINDOWS else ["-lpthread"]))
    print(f"cpp: {exe} ({time.time() - t0:.1f}s)")
    return exe


# --- running -------------------------------------------------------------------

BINARIES = {
    "goose": lambda: BUILD / "goose" / ("benchmark" + EXE),
    "rust": lambda: BUILD / "rust-target" / "release" / ("benchmarks" + EXE),
    "cpp": lambda: BUILD / "cpp" / ("benchmark" + EXE),
}
LINE = re.compile(r"^(\S+): (OK|ERR\S*.*?) in ([0-9.]+)s")


def pin_cpus(spec):
    """Restrict this process, and so every benchmark it starts, to the CPUs in
    `spec` ("0-15"). On a CPU with two core complexes of different cache sizes
    the times otherwise depend on where the scheduler happens to put a run."""
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


def run_suite(lang, config, only=None, exe=None):
    exe = exe or BINARIES[lang]()
    if not Path(exe).exists():
        sys.exit(f"{lang}: {exe} not built")
    results = {}
    names = only or [None]
    for name in names:
        argv = [exe, config] + ([name] if name else [])
        r = subprocess.run([str(a) for a in argv], cwd=ARENA / ("rust" if lang == "rust" else "cpp"),
                           capture_output=True, text=True, errors="replace")
        for line in r.stdout.splitlines():
            m = LINE.match(line.strip())
            if m:
                results[m.group(1)] = (m.group(2) == "OK", float(m.group(3)), line.strip())
        if r.returncode != 0 and not results:
            sys.stderr.write(f"{lang} exited {r.returncode}\n{r.stdout[-2000:]}{r.stderr[-2000:]}\n")
    return results


def db_path(tag=None):
    return BUILD / (f"results-{tag}.json" if tag else "results.json")


def load_db(tag=None):
    p = db_path(tag)
    return json.loads(p.read_text()) if p.exists() else {}


def save_db(db, tag=None):
    db_path(tag).write_text(json.dumps(db, indent=1))


def cmd_run(args):
    if args.exe:
        args.exe = str(Path(args.exe).resolve())
    # A tagged run keeps its own file, so concurrent runs never share one.
    db = load_db(args.tag)
    langs = args.langs.split(",") if args.langs else LANGS
    only = args.only.split(",") if args.only else None
    config = str(ARENA / ("test.js" if args.test else "run.js"))
    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
    for rep in range(args.reps):
        for lang in langs:
            res = run_suite(lang, config, only, args.exe if lang == "goose" else None)
            key = f"{args.tag}:{lang}" if args.tag else lang
            bad = [n for n, (ok, _, _) in res.items() if not ok]
            print(f"[{rep + 1}/{args.reps}] {key}: {len(res)} benchmarks, {sum(t for _, t, _ in res.values()):.3f}s"
                  + (f", FAILED: {', '.join(bad)}" if bad else ""))
            for n in (res if args.test else bad):
                print("   ", res[n][2])
            if args.test:
                continue
            for n, (ok, t, _) in res.items():
                db.setdefault(key, {}).setdefault(n, [])
                db[key][n].append({"t": t, "ok": ok, "at": stamp})
    if not args.test:
        save_db(db, args.tag)
        report(db, [f"{args.tag}:{l}" if args.tag else l for l in langs], only)


def report(db, langs=None, only=None, last=None):
    langs = langs or [l for l in db]
    order = []
    for line in json.loads((ARENA / "run.js").read_text()):
        order.append(line["name"])
    def best(lang, n):
        rs = db.get(lang, {}).get(n, [])
        if last:
            rs = rs[-last:]
        rs = [r["t"] for r in rs if r["ok"]]
        return min(rs) if rs else None
    def is_goose(l):
        return l.split(":")[-1] == "goose" or l.startswith("goose")
    others = [l for l in langs if not is_goose(l)]
    gooses = [l for l in langs if is_goose(l)]
    hdr = "| benchmark | " + " | ".join(langs) + " | " + " | ".join(
        f"{g}/{o}" for g in gooses for o in others) + " |"
    print(hdr)
    print("|" + "---|" * (hdr.count("|") - 1))
    tot = {l: 0.0 for l in langs}
    import math
    logs = {(g, o): [] for g in gooses for o in others}
    for n in order:
        if only and not any(o.lower() in n.lower() for o in only):
            continue
        ts = {l: best(l, n) for l in langs}
        if all(v is None for v in ts.values()):
            continue
        cells = [f"{ts[l]:.3f}" if ts[l] is not None else "-" for l in langs]
        ratios = []
        for g in gooses:
            for o in others:
                if ts[g] and ts[o]:
                    r = ts[o] / ts[g]
                    logs[(g, o)].append(math.log(r))
                    ratios.append(f"{r:.2f}" + (" **" if r < 0.9 else ""))
                else:
                    ratios.append("-")
        for l in langs:
            tot[l] += ts[l] or 0
        print(f"| {n} | " + " | ".join(cells) + " | " + " | ".join(ratios) + " |")
    geo = [f"{math.exp(sum(v) / len(v)):.2f}" if v else "-" for v in logs.values()]
    print("| **total / geomean** | " + " | ".join(f"{tot[l]:.2f}" for l in langs) + " | " + " | ".join(geo) + " |")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("langs", nargs="*")
    b.add_argument("--entry", default="main.goose", help="Goose entry file under goose/src")
    b.add_argument("--tag", default="goose", help="Goose build directory under build/")
    b.add_argument("--goose-flags", default="", help="extra goose flags, space separated")
    b.add_argument("--cflags", default="", help="extra clang flags, space separated")
    b.add_argument("--goose", help="the goose compiler to use instead of this checkout's build")
    b.add_argument("--src", help="the Goose sources to build instead of goose/src")
    r = sub.add_parser("run")
    r.add_argument("--langs")
    r.add_argument("--only")
    r.add_argument("--reps", type=int, default=3)
    r.add_argument("--tag", help="file results under this name instead of the language")
    r.add_argument("--exe", help="a Goose benchmark binary other than the default build")
    r.add_argument("--test", action="store_true", help="test.js instead of run.js")
    t = sub.add_parser("test")
    t.add_argument("langs", nargs="*")
    t.add_argument("--only")
    t.add_argument("--exe", help="a Goose benchmark binary other than the default build")
    rp = sub.add_parser("report")
    rp.add_argument("--langs")
    rp.add_argument("--only")
    rp.add_argument("--last", type=int, help="only the last N measurements per cell")
    rp.add_argument("--tag", help="the results file of tagged runs")
    ap.add_argument("--cpus", default=os.environ.get("BENCH_CPUS", "16-31"),
                    help="CPUs to pin benchmark runs to (default 16-31: on the 9950X3D this was measured on, the CCD without V-cache, whose 32 MB L3 matches LangArena's 3800X)")
    args = ap.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    if args.cmd in ("run", "test") and args.cpus != "all":
        pin_cpus(args.cpus)
    if args.cmd == "build":
        for l in args.langs or LANGS:
            if l == "goose":
                build_goose(args.goose_flags.split(), args.cflags.split(), args.tag, args.entry, args.goose, args.src)
            else:
                {"rust": build_rust, "cpp": build_cpp}[l]()
    elif args.cmd == "test":
        args.langs = ",".join(args.langs) if args.langs else None
        args.reps, args.tag, args.test = 1, None, True
        cmd_run(args)
    elif args.cmd == "run":
        cmd_run(args)
    elif args.cmd == "report":
        report(load_db(args.tag), args.langs.split(",") if args.langs else None,
               args.only.split(",") if args.only else None, args.last)


if __name__ == "__main__":
    main()
