#!/usr/bin/env python3
"""Build, test and time the Programming-Language-Benchmarks problems in Goose
against that repository's own Rust, C and C++ programs.

The Goose programs live here, one directory per problem, named as PLB names
its sources (`1.goose`, `2-m.goose`: `-m` for multithreaded, `-i` for
intrinsics). Everything else -- the problem list, the inputs, the expected
outputs, the data files and the other languages' sources -- is read from a
PLB checkout: `$PLB`, or a `Programming-Language-Benchmarks` or `PLB`
directory beside this repository's checkout. Build products go under
`bench/plb/build/`.

Programs are built the way PLB builds C (`clang -O3 -march=broadwell
-fomit-frame-pointer`), Rust with its Cargo profile (LTO, one codegen unit,
`-Ctarget-cpu=broadwell`), and the generated Goose C with the C flags, its
`simd fn`s capped at AVX2 to match.

  python bench/plb/plb.py build                       # every Goose program
  python bench/plb/plb.py build --langs rust,cpp,c    # the reference programs
  python bench/plb/plb.py test                        # Goose against the expected outputs
  python bench/plb/plb.py test nbody --langs rust
  python bench/plb/plb.py bench nbody,lru --reps 5    # timings and peak memory
  python bench/plb/plb.py report                      # the table from saved results
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

import yaml

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BUILD = HERE / "build"
sys.path.insert(0, str(REPO / "scripts"))
import toolchain as tc  # noqa: E402

IS_WINDOWS = os.name == "nt"
# What a program's runtime links besides libm and threads: dlopen, in libdl on
# Linux before glibc 2.34.
GOOSE_LIBS = ["-ldl"] if sys.platform.startswith("linux") else []
EXE = ".exe" if IS_WINDOWS else ""
LANGS = ["goose", "rust", "c", "cpp"]

# PLB's include/c/app.rsp and include/cpp/app.rsp, without -pipe and -Wall.
CFLAGS = ["-O3", "-fomit-frame-pointer", "-march=broadwell", "-w"] + \
    (["-fstrict-aliasing"] if IS_WINDOWS else [])
CXXFLAGS = CFLAGS + ["-std=c++20"]
OPENMP = ["-fopenmp"]
GOOSE_FLAGS = ["-O2"]
# The references are built for broadwell, and PLB's machines have no AVX-512,
# so `simd fn` versions stop at AVX2 unless --cflags=-DGS_SIMD=2 asks for more.
GOOSE_CFLAGS = CFLAGS + ["-DGS_SIMD=1"]


def find_plb():
    env = os.environ.get("PLB")
    if env:
        return Path(env).resolve()
    # A worktree lives under <checkout>/.claude/worktrees/<name>; PLB is a
    # sibling of the checkout, not of the worktree.
    for base in [REPO] + list(REPO.parents):
        for name in ("Programming-Language-Benchmarks", "PLB"):
            cand = base.parent / name
            if (cand / "bench" / "bench.yaml").exists():
                return cand
    sys.exit("PLB checkout not found: set PLB")


PLB = find_plb()
ALGO = PLB / "bench" / "algorithm"


def problems():
    cfg = yaml.safe_load((PLB / "bench" / "bench.yaml").read_text())
    return {p["name"]: p for p in cfg["problems"]}


def lang_sources(lang):
    """{problem: [source file names]} for one language."""
    if lang == "goose":
        out = {}
        for d in sorted(HERE.iterdir()):
            if d.is_dir() and d.name != "build":
                srcs = sorted(f.name for f in d.glob("*.goose"))
                if srcs:
                    out[d.name] = srcs
        return out
    cfg = yaml.safe_load((PLB / "bench" / f"bench_{lang}.yaml").read_text())
    return {p["name"]: list(p["source"]) for p in cfg["problems"]}


def vs_llvm_bin():
    if not IS_WINDOWS:
        return None
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
        "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return None
    r = subprocess.run([str(vswhere), "-latest", "-prerelease", "-property", "installationPath"],
                       capture_output=True, text=True)
    p = Path(r.stdout.strip().splitlines()[0]) / "VC" / "Tools" / "Llvm" / "x64" / "bin"
    return p if p.exists() else None


def clang_tools():
    b = vs_llvm_bin()
    if b:
        return str(b / "clang.exe"), str(b / "clang++.exe")
    cc, cxx = shutil.which("clang"), shutil.which("clang++")
    if not cc or not cxx:
        sys.exit("clang/clang++ not found")
    return cc, cxx


def openmp_libs():
    """clang on Windows links LLVM's libomp, which Visual Studio ships beside
    its clang; the import library is not on the default search path."""
    b = vs_llvm_bin()
    if b:
        lib = b.parent / "lib"
        return [f"-L{lib}", "-llibomp"], b
    return [], None


def goose_exe():
    for sub in ("Release", "", "RelWithDebInfo", "Debug"):
        p = REPO / "build" / sub / ("goose" + EXE)
        if p.exists():
            return p
    sys.exit("no goose binary under build/")


def run(argv, cwd=None, env=None):
    r = subprocess.run([str(a) for a in argv], cwd=cwd, env=env, capture_output=True, text=True,
                       errors="replace")
    return r.returncode == 0, r.stdout + r.stderr


def exe_path(lang, problem, src):
    return BUILD / lang / problem / (Path(src).stem + EXE)


# --- building --------------------------------------------------------------------

def build_goose(problem, src, args):
    cc, _ = clang_tools()
    exe = exe_path("goose", problem, src)
    exe.parent.mkdir(parents=True, exist_ok=True)
    cfile = exe.with_suffix(".c")
    ok, out = run([args.goose or goose_exe(), *GOOSE_FLAGS, *args.goose_flags.split(), "--standalone",
                   "-o", cfile, HERE / problem / src])
    if not ok:
        return False, out
    return run([cc, *GOOSE_CFLAGS, *args.cflags.split(), cfile, "-o", exe] +
               ([] if IS_WINDOWS else ["-lm", "-pthread"] + GOOSE_LIBS))


def build_c(lang, problem, src, args):
    cc, cxx = clang_tools()
    exe = exe_path(lang, problem, src)
    exe.parent.mkdir(parents=True, exist_ok=True)
    text = (ALGO / problem / src).read_text(errors="replace")
    uses_omp = "#pragma omp" in text or "omp.h" in text
    omp, _ = openmp_libs() if uses_omp else ([], None)
    flags = (CXXFLAGS if lang == "cpp" else CFLAGS) + (OPENMP + omp if uses_omp else [])
    if IS_WINDOWS:
        flags += [f"-I{HERE / 'winshim'}", "-include", "plb_win.h"]
    return run([cxx if lang == "cpp" else cc, *flags, ALGO / problem / src, "-o", exe] +
               ([] if IS_WINDOWS else ["-lm", "-pthread"]))


RUST_SKIP_DEPS = ("axum", "hyper", "rustls", "warp")
# "0" picks up pre-releases whose API the sources no longer match.
RUST_PINS = {"k256": "0.13", "elliptic-curve": "0.13"}


def rust_project():
    """One Cargo package for every Rust source, each a binary of its own, with
    PLB's dependencies and release profile minus the HTTP stack."""
    import tomllib
    proj = BUILD / "rust"
    (proj / "src" / "bin").mkdir(parents=True, exist_ok=True)
    cfg = tomllib.loads((PLB / "bench" / "include" / "rust" / "Cargo.toml").read_text())

    def val(v):
        if isinstance(v, str):
            return json.dumps(v)
        if isinstance(v, bool):
            return "true" if v else "false"
        if isinstance(v, list):
            return "[" + ", ".join(val(x) for x in v) + "]"
        if isinstance(v, dict):
            return "{ " + ", ".join(f"{k} = {val(x)}" for k, x in v.items()) + " }"
        return str(v)

    deps = {k: v for k, v in cfg["dependencies"].items() if not k.startswith(RUST_SKIP_DEPS)}
    deps.update(RUST_PINS)
    feats = [f for f in cfg["features"]["default"] if not f.startswith(RUST_SKIP_DEPS)]
    text = "\n".join([
        "[package]", 'edition = "2021"', 'name = "_app"', 'version = "0.1.0"', "",
        "[workspace]", "", "[features]", f"default = {val(feats)}", "",
        "[dependencies]", *(f"{k} = {val(v)}" for k, v in deps.items()), "",
        "[profile.release]", *(f"{k} = {val(v)}" for k, v in cfg["profile"]["release"].items()), ""])
    if not (proj / "Cargo.toml").exists() or (proj / "Cargo.toml").read_text() != text:
        (proj / "Cargo.toml").write_text(text)
    tc_file = PLB / "bench" / "include" / "rust" / "rust-toolchain.toml"
    shutil.copy(tc_file, proj / "rust-toolchain.toml")
    return proj


def build_rust(problem, src, args):
    proj = rust_project()
    name = f"{problem.replace('-', '_')}_{Path(src).stem.replace('-', '_')}"
    dst = proj / "src" / "bin" / f"{name}.rs"
    data = (ALGO / problem / src).read_bytes()
    if not dst.exists() or dst.read_bytes() != data:
        dst.write_bytes(data)
    env = dict(os.environ, RUSTFLAGS="-Ctarget-cpu=broadwell", CARGO_TARGET_DIR=str(proj / "target"))
    ok, out = run(["cargo", "build", "--release", "--bin", name], cwd=proj, env=env)
    if not ok:
        return False, out
    exe = exe_path("rust", problem, src)
    exe.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(proj / "target" / "release" / (name + EXE), exe)
    return True, out


def cmd_build(args):
    for lang in args.langs:
        srcs = lang_sources(lang)
        for problem in args.problems or sorted(srcs):
            for src in srcs.get(problem, []):
                if args.variants and Path(src).stem not in args.variants:
                    continue
                t0 = time.time()
                if lang == "goose":
                    ok, out = build_goose(problem, src, args)
                elif lang == "rust":
                    ok, out = build_rust(problem, src, args)
                else:
                    ok, out = build_c(lang, problem, src, args)
                status = "ok" if ok else "FAILED"
                print(f"{lang:5} {problem}/{src}: {status} ({time.time() - t0:.1f}s)")
                if not ok:
                    print("   " + "\n   ".join(out.strip().splitlines()[-25:]))


# --- running ---------------------------------------------------------------------

def setup_data(problem, cfg):
    """Unpack or copy a problem's data files into build/data/<problem>, which
    stands in for PLB's temporary directory; returns it, or None."""
    data = cfg.get("data") or []
    if not data:
        return None
    d = BUILD / "data" / problem
    d.mkdir(parents=True, exist_ok=True)
    for f in data:
        src = (ALGO / problem / f).resolve()
        if src.suffix == ".zip":
            with zipfile.ZipFile(src) as z:
                for n in z.namelist():
                    if not (d / n).exists():
                        z.extract(n, d)
        elif not (d / src.name).exists():
            shutil.copy(src, d / src.name)
    return d


def argv_for(cfg, inp):
    """PLB appends the input to the run command, joined to the data
    directory when the problem has data: `sample 10` becomes the arguments
    `<dir>/sample` and `10`."""
    d = setup_data(cfg["name"], cfg)
    text = str(d / str(inp)) if d else str(inp)
    return text.split()


def pin_cpus(spec):
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


def built_programs(args):
    probs = problems()
    for lang in args.langs:
        srcs = lang_sources(lang)
        for problem in args.problems or sorted(srcs):
            if problem not in probs:
                continue
            for src in srcs.get(problem, []):
                if args.variants and Path(src).stem not in args.variants:
                    continue
                exe = exe_path(lang, problem, src)
                if exe.exists():
                    yield lang, problem, src, exe, probs[problem]
                else:
                    print(f"{lang:5} {problem}/{src}: not built")


def cmd_test(args):
    failed = 0
    for lang, problem, src, exe, cfg in built_programs(args):
        for t in cfg.get("unittests", []):
            expected = (ALGO / problem / t["output"]).read_text(errors="replace")
            r = tc.run_measured(exe, argv_for(cfg, t["input"]))
            got = (exe.parent / (exe.name + ".stdout")).read_text(errors="replace")
            ok = r.code == 0 and got.rstrip() == expected.rstrip()
            failed += not ok
            print(f"{lang:5} {problem}/{src} [{t['input']}]: {'ok' if ok else 'FAILED'} ({r.ms:.0f} ms)")
            if not ok:
                diff = [f"line {i + 1}: got {a!r}, expected {b!r}"
                        for i, (a, b) in enumerate(zip(got.splitlines(), expected.splitlines())) if a != b]
                print("   exit", r.code, (r.err or "").strip()[:300])
                for line in diff[:5]:
                    print("  ", line)
                if len(got.splitlines()) != len(expected.splitlines()):
                    print(f"   {len(got.splitlines())} lines, expected {len(expected.splitlines())}")
    if failed:
        sys.exit(f"{failed} failed")


# One file per problem, so that runs of different problems can go on at once.
def db_path(problem):
    return BUILD / "results" / f"{problem}.json"


def load_db(problem):
    p = db_path(problem)
    return json.loads(p.read_text()) if p.exists() else {}


def cmd_bench(args):
    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
    progs = list(built_programs(args))
    # Inputs in PLB's order, every program of a problem interleaved, so that
    # machine drift spreads over all of them alike.
    for problem in dict.fromkeys(p for _, p, _, _, _ in progs):
        cfg = problems()[problem]
        db = load_db(problem)
        tests = cfg.get("tests", [])
        if args.input:
            tests = [t for t in tests if str(t["input"]) == args.input]
        for t in tests:
            argv = argv_for(cfg, t["input"])
            mine = [p for p in progs if p[1] == problem]
            best = {}
            for p in mine:   # warm-up: a fresh executable is scanned on first run
                tc.run_measured(p[3], argv)
            for rep in range(args.reps):
                for lang, _, src, exe, _ in mine:
                    r = tc.run_measured(exe, argv)
                    key = f"{lang}/{src}"
                    if r.code != 0:
                        best[key] = None
                        continue
                    if key in best and best[key] is None:
                        continue
                    ms, peak = best.get(key, (1e18, 0))
                    best[key] = (min(ms, r.ms), max(peak, r.peak))
            for key, v in best.items():
                if v is None:
                    print(f"{problem} [{t['input']}] {key}: FAILED")
                    continue
                db.setdefault(str(t["input"]), {}).setdefault(key, []).append(
                    {"ms": v[0], "peak": v[1], "at": stamp})
            db_path(problem).parent.mkdir(parents=True, exist_ok=True)
            db_path(problem).write_text(json.dumps(db, indent=1))
            report_one(problem, t["input"], {k: v for k, v in best.items() if v})


def fmt_mb(b):
    return f"{b / 1048576:.1f}"


def report_one(problem, inp, rows):
    if not rows:
        return
    fastest = min(ms for ms, _ in rows.values())
    print(f"\n{problem} {inp}")
    for key, (ms, peak) in sorted(rows.items(), key=lambda kv: kv[1][0]):
        print(f"  {key:28} {ms:9.1f} ms  {fmt_mb(peak):>8} MB  x{ms / fastest:.2f}")


# A program is compared with the others of its kind: one that runs several
# threads, whether its name says so (PLB's -m) or not (OpenMP, rayon, a
# multithreaded async runtime), only with the multithreaded ones.
MT_MARKS = re.compile(r"#pragma omp|rayon::|\.par_\w+|into_par_iter|thread::spawn|tokio::main\]|"
                      r"task::spawn|thread_spawn")


def multithreaded(lang, problem, src):
    if re.search(r"-\w*m$", Path(src).stem):
        return True
    path = HERE / problem / src if lang == "goose" else ALGO / problem / src
    return bool(MT_MARKS.search(path.read_text(errors="replace")))


def best_rows(problem, rows, langs, last):
    best = {}
    for key, rs in rows.items():
        if langs and key.split("/")[0] not in langs:
            continue
        rs = rs[-last:] if last else rs
        best[key] = (min(r["ms"] for r in rs), max(r["peak"] for r in rs))
    return best


def markdown(problems_):
    """One row per input: the fastest program of each language, single- and
    multithreaded, and Goose's speed relative to the fastest other one (above
    1 means Goose is faster)."""
    groups = [("goose", "Goose"), ("rust", "Rust"), (("c", "cpp"), "C/C++")]
    print("| problem | input | " + " | ".join(n for _, n in groups) + " | ratio | " +
          " | ".join(f"{n} -m" for _, n in groups) + " | ratio -m |")
    print("|---|---|" + "---:|" * (2 * len(groups) + 2))
    for problem in problems_:
        for inp, rows in load_db(problem).items():
            best = best_rows(problem, rows, None, None)

            def pick(langs, mt):
                cands = [(ms, peak, key) for key, (ms, peak) in best.items()
                         if key.split("/")[0] in (langs if isinstance(langs, tuple) else (langs,))
                         and multithreaded(key.split("/")[0], problem, key.split("/", 1)[1]) == mt]
                return min(cands) if cands else None

            cells = []
            for mt in (False, True):
                found = [pick(l, mt) for l, _ in groups]
                for f in found:
                    cells.append(f"{f[0]:.0f} <sub>{Path(f[2].split('/', 1)[1]).stem} {fmt_mb(f[1])}M</sub>"
                                 if f else "-")
                others = [f[0] for f in found[1:] if f]
                cells.append(f"{min(others) / found[0][0]:.2f}" if found[0] and others else "-")
            print(f"| {problem} | {inp} | " + " | ".join(cells) + " |")


def cmd_report(args):
    probs = args.problems or sorted(p.stem for p in (BUILD / "results").glob("*.json"))
    if args.markdown:
        markdown(probs)
        return
    for problem in probs:
        for inp, rows in load_db(problem).items():
            report_one(problem, inp, best_rows(problem, rows, args.langs, args.last))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["build", "test", "bench", "report"])
    ap.add_argument("problems", nargs="?", default="", help="comma-separated problems (default all)")
    ap.add_argument("--langs", default=None, help="comma-separated: goose,rust,c,cpp")
    ap.add_argument("--variants", default="", help="comma-separated source stems, e.g. 1,2-m")
    ap.add_argument("--goose", help="the goose compiler to use instead of this checkout's build")
    ap.add_argument("--goose-flags", default="")
    ap.add_argument("--cflags", default="")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--input", help="bench only this input of each problem")
    ap.add_argument("--last", type=int, help="report: only the last N measurements per cell")
    ap.add_argument("--markdown", action="store_true", help="report: the summary table of README.md")
    ap.add_argument("--cpus", default=os.environ.get("BENCH_CPUS", "16-31"),
                    help="CPUs to pin runs to (default 16-31, the CCD without V-cache on the 9950X3D)")
    args = ap.parse_args()
    args.problems = [p for p in args.problems.split(",") if p]
    args.variants = [v for v in args.variants.split(",") if v]
    default_langs = ["goose"] if args.cmd in ("build", "test") else LANGS
    args.langs = args.langs.split(",") if args.langs else default_langs
    BUILD.mkdir(parents=True, exist_ok=True)
    if args.cmd in ("test", "bench") and args.cpus != "all":
        pin_cpus(args.cpus)
    _, ompdir = openmp_libs()
    if ompdir:   # the OpenMP runtime DLL, for the C and C++ programs that use it
        os.environ["PATH"] = str(ompdir) + os.pathsep + os.environ["PATH"]
    {"build": cmd_build, "test": cmd_test, "bench": cmd_bench, "report": cmd_report}[args.cmd](args)


if __name__ == "__main__":
    main()
