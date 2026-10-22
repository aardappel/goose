#!/usr/bin/env python3
"""Build and run the bddicken/languages benchmarks locally in Goose, C, C++ and Rust.

The Goose implementation lives here, laid out as it would sit in the
bddicken/languages repository (`lib/goose/` and `<benchmark>/goose/`). The C,
C++ and Rust implementations are read from a checkout of that repository (by
default `languages` or `bddicken-languages` next to the goose checkout, or
`$BDDICKEN`), and every build product goes under `bench/bddicken/build/`.

  python bench/bddicken/compare.py build                  # all languages
  python bench/bddicken/compare.py build goose            # one of them
  python bench/bddicken/compare.py check                  # each program's check-output run
  python bench/bddicken/compare.py run --reps 3           # timings and a ratio table
  python bench/bddicken/compare.py run --langs goose,c --only fibonacci --run-ms 3000

The flags are those of the repository's languages.sh: C with -O3, C++ with
-O3 -march=native -ffast-math (its -Ofast), Rust with its Cargo.toml
release profile. Goose's C is compiled as the C version is; the extra
languages goose-native and c-native add -march=native, as C++ has.

  python bench/bddicken/compare.py build --langs goose-native,c-native On Windows the compilers
are the clang and clang++ bundled with Visual Studio; the reference C runner's
clock_gettime comes from shim/clock_gettime.h there.

The in-process benchmarks print mean, standard deviation, min and max of their
own timing; hello-world is timed as whole processes, as the repository's
runner does with hyperfine.
"""

import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BUILD = HERE / "build"
IS_WINDOWS = os.name == "nt"
# What a program's runtime links besides libm and threads: dlopen, in libdl on
# Linux before glibc 2.34.
GOOSE_LIBS = ["-ldl"] if sys.platform.startswith("linux") else []
EXE = ".exe" if IS_WINDOWS else ""

LANGS = ["goose", "c", "cpp", "rust"]
BENCHES = ["fibonacci", "loops", "levenshtein", "hello-world"]


def find_languages():
    env = os.environ.get("BDDICKEN")
    if env:
        return Path(env).resolve()
    # A worktree lives under <checkout>/.claude/worktrees/<name>; the clone is
    # a sibling of the checkout, not of the worktree.
    for base in [REPO] + list(REPO.parents):
        for name in ("languages", "bddicken-languages"):
            cand = base.parent / name
            if (cand / "languages.sh").exists():
                return cand
    sys.exit("bddicken/languages checkout not found: set BDDICKEN")


LANGUAGES = find_languages()

# The input each benchmark gets (its check-output.sh -i), and the check of
# the result field (its check-output.sh).
INPUTS = {
    "fibonacci": "37",
    "loops": "40",
    "levenshtein": str(LANGUAGES / "levenshtein" / "levenshtein-words.txt"),
    "hello-world": "",
}


def check_result(bench, out):
    if bench == "hello-world":
        return out.strip().lower() == "hello, world!"
    fields = out.strip().splitlines()[0].split(",") if out.strip() else []
    if len(fields) != 6:
        return False
    r = fields[5].strip()
    if bench == "fibonacci":
        return r == "24157817"
    if bench == "loops":
        return r.isdigit() and 195000 <= int(r) <= 204999
    if bench == "levenshtein":
        return r == "554324"
    return False


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
    cc, cxx = shutil.which("clang") or shutil.which("gcc"), shutil.which("clang++") or shutil.which("g++")
    if not cc or not cxx:
        sys.exit("no C or C++ compiler found")
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


def exe_path(lang, bench):
    return BUILD / lang / (bench + EXE)


C_FLAGS = ["-O3", "-w"]
CPP_FLAGS = ["-std=c++23", "-O3", "-ffast-math", "-march=native", "-w"]
LIBM = [] if IS_WINDOWS else ["-lm"]


def build_goose(bench, native, compiler=None, tag=None, goose_flags=("-O2",)):
    cc, _ = clang_tools()
    out = BUILD / (tag or ("goose-native" if native else "goose"))
    out.mkdir(parents=True, exist_ok=True)
    cfile = out / f"{bench}.c"
    run([compiler or goose_exe(), *goose_flags, "--standalone", "-o", cfile, HERE / bench / "goose" / "run.goose"])
    flags = C_FLAGS + (["-march=native"] if native else [])
    run([cc, *flags, cfile, "-o", out / (bench + EXE)] + ([] if IS_WINDOWS else ["-lm", "-pthread"] + GOOSE_LIBS))


def build_c(bench, native):
    cc, _ = clang_tools()
    src = LANGUAGES / bench / "c"
    out = BUILD / ("c-native" if native else "c")
    out.mkdir(parents=True, exist_ok=True)
    flags = C_FLAGS + (["-march=native"] if native else [])
    shim = ["-include", HERE / "shim" / "clock_gettime.h"] if IS_WINDOWS else []
    if bench == "hello-world":
        files = [src / "run.c"]
        shim = []
    else:
        files = [LANGUAGES / "lib" / "c" / "benchmark.c"] + sorted(src.glob("*.c"))
    run([cc, *flags, *shim, "-I", LANGUAGES / "lib" / "c", *files, "-o", out / (bench + EXE)] + LIBM)


def build_cpp(bench):
    _, cxx = clang_tools()
    out = BUILD / "cpp"
    out.mkdir(parents=True, exist_ok=True)
    run([cxx, *CPP_FLAGS, "-I", LANGUAGES / "lib" / "cpp", LANGUAGES / bench / "cpp" / "run.cpp",
         "-o", out / (bench + EXE)])


def build_rust(bench):
    target = BUILD / "rust-target" / bench
    env = dict(os.environ, CARGO_TARGET_DIR=str(target))
    run(["cargo", "build", "--release", "--bin", "run", "--manifest-path",
         LANGUAGES / bench / "rust" / "Cargo.toml"], env=env)
    out = BUILD / "rust"
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy2(target / "release" / ("run" + EXE), out / (bench + EXE))


def cmd_build(args):
    langs = args.langs or LANGS
    for bench in args.benches:
        for lang in langs:
            t0 = time.time()
            if lang in ("goose", "goose-native"):
                build_goose(bench, lang.endswith("-native"))
            elif lang in ("c", "c-native"):
                build_c(bench, lang.endswith("-native"))
            elif lang == "cpp":
                build_cpp(bench)
            elif lang == "rust":
                build_rust(bench)
            print(f"{lang:6} {bench:12} built in {time.time() - t0:.1f}s")


def pin_cpus(spec):
    """Restrict this process, and so every benchmark it starts, to the CPUs in
    spec ("16-31", "0,2,4")."""
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


def run_bench(lang, bench, run_ms, warmup_ms, exe=None):
    exe = exe or exe_path(lang, bench)
    if not exe.exists():
        return None
    r = run([exe, run_ms, warmup_ms, INPUTS[bench]], cwd=LANGUAGES / bench, check=False)
    return r.stdout


def cmd_check(args):
    langs = args.langs or LANGS
    bad = 0
    for bench in args.benches:
        for lang in langs:
            out = run_bench(lang, bench, 1, 0)
            if out is None:
                print(f"{lang:6} {bench:12} not built")
                continue
            ok = check_result(bench, out)
            bad += not ok
            print(f"{lang:6} {bench:12} {'ok' if ok else 'FAILED'}  {out.strip().splitlines()[0] if out.strip() else ''}")
    if bad:
        sys.exit(1)


def time_processes(exe, n):
    """Whole-process wall times in ms, as hyperfine measures hello-world."""
    times = []
    for i in range(n):
        t0 = time.perf_counter()
        subprocess.run([str(exe), "1", "0"], stdout=subprocess.DEVNULL)
        times.append((time.perf_counter() - t0) * 1000.0)
    return times


def cmd_run(args):
    langs = args.langs or LANGS
    db = load_db()
    for rep in range(args.reps):
        # Interleaved: every rep runs every language once per benchmark, so
        # slow drifts of the machine hit all of them alike.
        for bench in args.benches:
            for lang in langs:
                exe = exe_path(lang, bench)
                if not exe.exists():
                    continue
                if bench == "hello-world":
                    ts = time_processes(exe, args.hello_runs)
                    rec = {"mean": statistics.mean(ts), "min": min(ts), "runs": len(ts)}
                else:
                    out = run_bench(lang, bench, args.run_ms, args.warmup_ms)
                    fields = out.strip().splitlines()[0].split(",")
                    if not check_result(bench, out):
                        print(f"{lang} {bench}: wrong result {fields[5] if len(fields) > 5 else out!r}")
                    rec = {"mean": float(fields[0]), "sd": float(fields[1]), "min": float(fields[2]),
                           "max": float(fields[3]), "runs": int(fields[4])}
                db.setdefault(bench, {}).setdefault(lang, []).append(rec)
                print(f"rep {rep + 1}: {lang:6} {bench:12} mean {rec['mean']:9.3f} ms  min {rec['min']:9.3f} ms  ({rec['runs']} runs)")
        save_db(db)
    report(db, langs, args.benches)


def db_path():
    return BUILD / "results.json"


def load_db():
    p = db_path()
    return json.loads(p.read_text()) if p.exists() else {}


def save_db(db):
    BUILD.mkdir(parents=True, exist_ok=True)
    db_path().write_text(json.dumps(db, indent=1))


def report(db, langs, benches):
    """The best mean per benchmark and language over the reps run, and Goose's
    ratio against each other language (above 1.0: Goose is faster)."""
    others = [l for l in langs if l != "goose"]
    head = f"| benchmark | " + " | ".join(langs) + " | " + " | ".join(f"vs {l}" for l in others) + " |"
    print()
    print(head)
    print("|" + "---|" * (1 + len(langs) + len(others)))
    for bench in benches:
        best = {}
        for l in langs:
            recs = db.get(bench, {}).get(l)
            if recs:
                best[l] = min(r["mean"] for r in recs)
        cells = [f"{best[l]:.3f}" if l in best else "-" for l in langs]
        ratios = [f"{best[l] / best['goose']:.2f}" if l in best and "goose" in best else "-" for l in others]
        print(f"| {bench} | " + " | ".join(cells) + " | " + " | ".join(ratios) + " |")


def cmd_report(args):
    report(load_db(), args.langs or LANGS, args.benches)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["build", "check", "run", "report"])
    ap.add_argument("lang", nargs="?", help="a single language to build or check")
    ap.add_argument("--langs", help="comma-separated: " + ",".join(LANGS))
    ap.add_argument("--only", help="comma-separated benchmarks: " + ",".join(BENCHES))
    ap.add_argument("--run-ms", type=int, default=10000)
    ap.add_argument("--warmup-ms", type=int, default=2000)
    ap.add_argument("--reps", type=int, default=1)
    ap.add_argument("--hello-runs", type=int, default=200)
    ap.add_argument("--cpus", default=os.environ.get("BENCH_CPUS", "16-31"),
                    help="CPUs to pin runs to (default 16-31: the 9950X3D's CCD without V-cache), or all")
    args = ap.parse_args()
    args.langs = [args.lang] if args.lang else (args.langs.split(",") if args.langs else None)
    args.benches = args.only.split(",") if args.only else BENCHES
    if args.cmd in ("run", "check") and args.cpus != "all":
        pin_cpus(args.cpus)
    {"build": cmd_build, "check": cmd_check, "run": cmd_run, "report": cmd_report}[args.cmd](args)


if __name__ == "__main__":
    main()
