"""Builds and runs the Goose tests of bench/kostya2 next to the C, C++ and
Rust tests of kostya/benchmarks (https://github.com/kostya/benchmarks), and
measures them the way that suite does.

The suite times a test between two TCP messages the test sends to
localhost:9001 (xtime.rb): one as the measured work starts, naming the test
and its process, one as it ends, so reading input and self-checks are left
out. Memory is the peak growth of the process's resident set over that
window. `run` does the same with a server of its own, and runs every test of
a group in turn, round after round, so drift in the machine spreads over all
of them; it reports the median of the rounds.

The C and C++ tests are built with the suite's own clang flags (common/
commands.mk), the Goose tests' C with the same ones, and the Rust tests with
cargo and the suite's RUSTFLAGS. One difference from the suite's Docker
image: C++ links libstdc++ where the suite asks for libc++, if libc++ is not
installed.

Linux is where the suite runs and what this is meant for (WSL works). On
Windows the C and C++ tests build against small shims in common/win, but
Windows runs these same programs several times slower than WSL does on the
same machine, so its numbers do not compare with the suite's.

    python3 bench/kostya2/compare.py build                 # everything
    python3 bench/kostya2/compare.py build goose           # just the Goose tests
    python3 bench/kostya2/compare.py check                 # outputs side by side
    python3 bench/kostya2/compare.py run --reps 5          # timings, all groups
    python3 bench/kostya2/compare.py run --only json,primes --reps 3

The suite's checkout is found at $KOSTYA, or next to this repository's
checkout as kostya-benchmarks/ or benchmarks/. The build goes to
bench/kostya2/build, or --build-dir.
"""

import argparse
import os
import shutil
import socket
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
IS_WINDOWS = os.name == "nt"
# What a program's runtime links besides libm and threads: dlopen, in libdl on
# Linux before glibc 2.34.
GOOSE_LIBS = ["-ldl"] if sys.platform.startswith("linux") else []
EXE = ".exe" if IS_WINDOWS else ""


def find_suite():
    if os.environ.get("KOSTYA"):
        return Path(os.environ["KOSTYA"])
    # Next to the checkout, or next to the one a worktree belongs to.
    for d in [REPO, *REPO.parents]:
        for name in ("kostya-benchmarks", "benchmarks"):
            p = d.parent / name
            if (p / "brainfuck" / "bench.b").exists():
                return p
    sys.exit("kostya/benchmarks not found: clone it next to this checkout or set KOSTYA")


SUITE = find_suite()
BUILD = HERE / "build"

# common/commands.mk
COMMON_FLAGS = ["-Wall", "-Wextra", "-pedantic", "-Wcast-align", "-O3", "-march=native",
                "-flto=auto", "-Wa,-mbranches-within-32B-boundaries"]
RUSTC_FLAGS = "-C target-cpu=native -C llvm-args=--x86-branches-within-32B-boundaries"

# The tests, by group: the binary (under BUILD), the arguments, and the input
# a group needs. A group's first entry is Goose's, which the ratios compare
# with.
GROUPS = {
    "brainfuck bench.b": [("goose", "goose/bf"), ("c", "c/bf"), ("c++", "cpp/bf"), ("rust", "rust/release/bf")],
    "brainfuck mandel.b": [("goose", "goose/bf"), ("c", "c/bf"), ("c++", "cpp/bf"), ("rust", "rust/release/bf")],
    "base64": [("goose", "goose/base64"), ("c", "c/base64_nolib"), ("c aklomp", "c/base64_aklomp"),
               ("rust", "rust/release/base64")],
    "json": [("goose", "goose/json"), ("goose dom", "goose/json_dom"), ("rust serde typed", "rust/release/json-struct-rs"),
             ("rust serde untyped", "rust/release/json-value-rs"), ("rust serde custom", "rust/release/json-pull-rs"),
             ("c++ rapidjson", "cpp/json_rapid"), ("c++ rapidjson precise", "cpp/json_rapid_precise"),
             ("c++ simdjson", "cpp/json_simdjson"), ("c++ simdjson dom", "cpp/json_simdjson_dom"),
             ("c yyjson", "c/json_yyjson")],
    "matmul": [("goose", "goose/matmul"), ("c", "c/matmul"), ("rust", "rust/release/matmul")],
    "primes": [("goose", "goose/primes"), ("c++", "cpp/primes"), ("rust", "rust/release/primes")],
}


def group_args(group):
    if group == "brainfuck bench.b":
        return [str(SUITE / "brainfuck" / "bench.b")]
    if group == "brainfuck mandel.b":
        return [str(SUITE / "brainfuck" / "mandel.b")]
    if group == "matmul":
        return ["1500"]   # MSIZE in matmul/Makefile
    return []


JSON_FILE = Path("/tmp/1.json")


def sh(argv, cwd=None, env=None, check=True):
    r = subprocess.run([str(a) for a in argv], cwd=cwd, env=env, capture_output=True, text=True,
                       errors="replace")
    if check and r.returncode != 0:
        sys.stderr.write(f"failed: {' '.join(str(a) for a in argv)}\n{r.stdout[-3000:]}{r.stderr[-3000:]}\n")
        sys.exit(1)
    return r


def tool(name):
    p = shutil.which(name)
    if not p and IS_WINDOWS:
        vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
            "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
        if vswhere.exists():
            root = sh([vswhere, "-latest", "-prerelease", "-property", "installationPath"]).stdout.strip()
            p = Path(root.splitlines()[0]) / "VC" / "Tools" / "Llvm" / "x64" / "bin" / (name + ".exe")
            p = str(p) if p.exists() else None
    if not p:
        sys.exit(f"{name} not found")
    return p


def cflags():
    f = list(COMMON_FLAGS)
    if IS_WINDOWS:
        # lld for -flto; the shims for the POSIX calls; MSVC's runtime warnings off.
        f += ["-fuse-ld=lld", "-fstrict-aliasing", "-D_CRT_SECURE_NO_WARNINGS", f"-I{HERE / 'common' / 'win'}"]
    return f + ["-w"]


def libnotify():
    """The suite's common/libnotify, or its Winsock stand-in."""
    out = BUILD / "libnotify"
    out.mkdir(parents=True, exist_ok=True)
    src = HERE / "common" / "win" / "libnotify.c" if IS_WINDOWS else SUITE / "common" / "libnotify" / "libnotify.c"
    obj = out / "libnotify.o"
    sh([tool("clang"), "-O2", "-w", f"-I{SUITE / 'common' / 'libnotify'}", "-c", src, "-o", obj])
    return [f"-I{SUITE / 'common' / 'libnotify'}", str(obj)]


# --- Goose ---------------------------------------------------------------------

GOOSE_TESTS = {
    "bf": "brainfuck/bf.goose",
    "base64": "base64/test.goose",
    "json": "json/test.goose",
    "json_dom": "json/test_dom.goose",
    "matmul": "matmul/matmul.goose",
    "primes": "primes/primes.goose",
}


def goose_exe():
    for sub in ("Release", "", "RelWithDebInfo"):
        p = REPO / "build" / sub / ("goose" + EXE)
        if p.exists():
            return p
    sys.exit("no goose binary under build/ (or pass --goose)")


def build_goose(args):
    goose = args.goose or goose_exe()
    out = BUILD / "goose"
    out.mkdir(parents=True, exist_ok=True)
    notify = libnotify()
    env = dict(os.environ, GOOSE_STDLIB=os.environ.get("GOOSE_STDLIB", str(REPO / "stdlib")))
    for name, src in GOOSE_TESTS.items():
        if args.only and name not in args.only:
            continue
        c = out / f"{name}.c"
        sh([goose, "-O2", "--standalone", "--include", HERE / "common" / "notify.h", "-o", c, HERE / src], env=env)
        sh([tool("clang"), *cflags(), "-std=c23", "-o", out / (name + EXE), c, *notify] +
           ([] if IS_WINDOWS else ["-lm", "-pthread"] + GOOSE_LIBS))
        print(f"goose: {name}")


# --- C and C++ -----------------------------------------------------------------

DEPS = {
    "rapidjson": "https://github.com/miloyip/rapidjson.git",
    "simdjson": "https://github.com/simdjson/simdjson.git",
    "yyjson": "https://github.com/ibireme/yyjson.git",
    "aklomp-base64": "https://github.com/aklomp/base64.git",
}


def dep(name):
    d = BUILD / "deps" / name
    if not d.exists():
        d.parent.mkdir(parents=True, exist_ok=True)
        sh(["git", "clone", "--depth", "1", "-q", DEPS[name], d])
    return d


def has_libcxx():
    r = subprocess.run([tool("clang++"), "-x", "c++", "-stdlib=libc++", "-E", "-"], input="#include <vector>\n",
                       capture_output=True, text=True)
    return r.returncode == 0


def build_c(args):
    cc, cxx = tool("clang"), tool("clang++")
    notify = libnotify()
    c_out, cpp_out = BUILD / "c", BUILD / "cpp"
    c_out.mkdir(parents=True, exist_ok=True)
    cpp_out.mkdir(parents=True, exist_ok=True)
    stdlib = ["-stdlib=libc++"] if not IS_WINDOWS and has_libcxx() else []
    if not stdlib and not IS_WINDOWS:
        print("c++: libc++ not installed, linking libstdc++")

    def c(out, srcs, extra=()):
        sh([cc, *cflags(), "-std=c23", "-o", c_out / (out + EXE), *srcs, *extra, *notify])
        print(f"c: {out}")

    def cpp(out, srcs, extra=()):
        sh([cxx, *cflags(), "-std=c++23", *stdlib, "-o", cpp_out / (out + EXE), *srcs, *extra, *notify])
        print(f"c++: {out}")

    s = SUITE
    c("bf", [s / "brainfuck" / "bf.c"])
    cpp("bf", [s / "brainfuck" / "bf.cpp"])
    c("base64_nolib", [s / "base64" / "test-nolib.c"])
    ak = dep("aklomp-base64")
    # aklomp's own codecs, each with the instruction set it is for, as its
    # Makefile builds them; its config.h says which ones this CPU may run.
    codec_flags = {"avx512": ["-mavx512vl", "-mavx512vbmi"], "avx2": ["-mavx2"], "ssse3": ["-mssse3"],
                   "sse41": ["-msse4.1"], "sse42": ["-msse4.2"], "avx": ["-mavx"], "generic": [],
                   "neon32": [], "neon64": []}
    (ak / "lib" / "config.h").write_text("".join(
        f"#define HAVE_{k.upper()} {0 if k.startswith('neon') else 1}\n" for k in codec_flags))
    objs = []
    for arch, fl in codec_flags.items():
        o = BUILD / "deps" / f"b64_{arch}.o"
        sh([cc, "-O3", "-w", "-std=c99", *fl, f"-I{ak / 'lib'}", f"-I{ak / 'include'}", "-c",
            ak / "lib" / "arch" / arch / "codec.c", "-o", o])
        objs.append(o)
    for name in ("lib", "codec_choose", "tables/tables"):
        o = BUILD / "deps" / f"b64_{name.replace('/', '_')}.o"
        sh([cc, "-O3", "-w", "-std=c99", f"-I{ak / 'lib'}", f"-I{ak / 'include'}", "-c", ak / "lib" / f"{name}.c", "-o", o])
        objs.append(o)
    c("base64_aklomp", [s / "base64" / "test-aklomp.c", *objs], [f"-I{ak / 'include'}"])
    rj = dep("rapidjson")
    cpp("json_rapid", [s / "json" / "test_rapid.cpp"], [f"-I{rj / 'include'}"])
    cpp("json_rapid_precise", [s / "json" / "test_rapid.cpp"], [f"-I{rj / 'include'}", "-DPRECISED"])
    sj = dep("simdjson") / "singleheader"
    cpp("json_simdjson", [s / "json" / "test_simdjson.cpp", sj / "simdjson.cpp"], [f"-I{sj}"])
    cpp("json_simdjson_dom", [s / "json" / "test_simdjson_dom.cpp", sj / "simdjson.cpp"], [f"-I{sj}"])
    yy = dep("yyjson") / "src"
    c("json_yyjson", [s / "json" / "test_yyjson.c", yy / "yyjson.c"], [f"-I{yy}"])
    c("matmul", [s / "matmul" / "matmul.c"], [] if IS_WINDOWS else ["-lm"])
    cpp("primes", [s / "primes" / "primes.cpp"])


# --- Rust ----------------------------------------------------------------------

RUST_CRATES = {"bf": "brainfuck/bf.rs", "base64": "base64/base64.rs", "json": "json/json.rs",
               "matmul": "matmul/matmul.rs", "primes": "primes/primes.rs"}


def build_rust(args):
    """The suite's crates, copied so that their path to common/rust holds,
    without the jq test, which needs libjq installed."""
    src = BUILD / "rust-src"
    if src.exists():
        shutil.rmtree(src)
    shutil.copytree(SUITE / "common" / "rust", src / "utils", ignore=shutil.ignore_patterns("target"))
    env = dict(os.environ, RUSTFLAGS=RUSTC_FLAGS, CARGO_TARGET_DIR=str(BUILD / "rust"))
    for name, path in RUST_CRATES.items():
        shutil.copytree(SUITE / path, src / name, ignore=shutil.ignore_patterns("target"))
        toml = src / name / "Cargo.toml"
        t = toml.read_text().replace('path = "../../common/rust"', 'path = "../utils"')
        t = t.replace('jq-rs = "0.4.1"\n', "").replace('[[bin]]\nname = "json-jq-rs"\npath = "src/json_jq.rs"\n', "")
        toml.write_text(t)
        sh(["cargo", "build", "-q", "--release", "--manifest-path", toml], env=env)
        print(f"rust: {name}")


# --- Measuring -----------------------------------------------------------------

class Runner:
    """The suite's xtime.rb: a server on localhost:9001 that times the window
    between a test's two messages and samples its resident set meanwhile."""

    def __init__(self):
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 9001))
        self.srv.listen(4)
        self.srv.settimeout(0.01)

    @staticmethod
    def rss(pid):
        try:
            with open(f"/proc/{pid}/statm") as f:
                return int(f.read().split()[1]) * os.sysconf("SC_PAGE_SIZE")
        except (OSError, ValueError):
            return 0

    def message(self, proc):
        while True:
            try:
                conn, _ = self.srv.accept()
            except socket.timeout:
                if proc.poll() is not None:
                    return None
                continue
            with conn:
                conn.settimeout(5)
                data = b""
                while True:
                    chunk = conn.recv(256)
                    if not chunk:
                        break
                    data += chunk
            return data.decode(errors="replace")

    def run(self, argv, env=None):
        """(seconds, base MiB, increase MiB, name, stdout) of one run."""
        out = []
        proc = subprocess.Popen([str(a) for a in argv], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        reader = threading.Thread(target=lambda: out.append(proc.stdout.read()))
        reader.start()
        start = self.message(proc)
        if start is None:
            reader.join()
            err = proc.stderr.read().decode(errors="replace")
            raise RuntimeError(f"{argv[0]} sent no start message (exit {proc.returncode}): {err[-500:]}")
        t0 = time.perf_counter_ns()
        name, _, pid = start.strip().partition("\t")
        pid = int(pid or proc.pid)
        base = peak = self.rss(pid)
        stop = None
        while stop is None:
            try:
                conn, _ = self.srv.accept()
                with conn:
                    conn.recv(256)
                stop = time.perf_counter_ns()
            except socket.timeout:
                peak = max(peak, self.rss(pid))
                if proc.poll() is not None:
                    raise RuntimeError(f"{argv[0]} exited without a stop message")
        reader.join()
        proc.wait()
        if proc.returncode != 0:
            raise RuntimeError(f"{argv[0]} exited {proc.returncode}: {proc.stderr.read().decode(errors='replace')[-500:]}")
        mib = 1 << 20
        return (stop - t0) / 1e9, base / mib, (peak - base) / mib, name, out[0].decode(errors="replace")


def pin(spec):
    if not spec or spec == "all":
        return
    cpus = set()
    for part in spec.split(","):
        lo, _, hi = part.partition("-")
        cpus.update(range(int(lo), int(hi or lo) + 1))
    if IS_WINDOWS:
        import ctypes
        k32 = ctypes.windll.kernel32
        k32.GetCurrentProcess.restype = ctypes.c_void_p
        k32.SetProcessAffinityMask(ctypes.c_void_p(k32.GetCurrentProcess()), ctypes.c_size_t(sum(1 << c for c in cpus)))
    else:
        os.sched_setaffinity(0, cpus)


def selected(only):
    gs = list(GROUPS)
    if only:
        gs = [g for g in gs if any(o.lower() in g.lower() for o in only)]
    return gs


def need_json():
    if not JSON_FILE.exists():
        print(f"writing {JSON_FILE}")
        sh([sys.executable, HERE / "json" / "generate_json.py", JSON_FILE])


def cmd_check(args):
    runner = Runner()
    for g in selected(args.only):
        if g == "json":
            need_json()
        print(f"== {g}")
        outs = {}
        for label, exe in GROUPS[g]:
            p = BUILD / (exe + EXE)
            if not p.exists():
                print(f"  {label}: not built")
                continue
            t, base, inc, name, out = runner.run([p, *group_args(g)])
            outs[label] = out
            last = out.strip().splitlines()[-1] if out.strip() else ""
            print(f"  {label:22} {name!r:28} {t:7.3f}s  {last[:110]}")
        if g.startswith("brainfuck"):
            same = len(set(outs.values())) == 1
            print(f"  outputs identical: {same}")


def cmd_run(args):
    pin(args.cpus)
    runner = Runner()
    for g in selected(args.only):
        if g == "json":
            need_json()
        impls = [(l, BUILD / (e + EXE)) for l, e in GROUPS[g] if (BUILD / (e + EXE)).exists()]
        if args.langs:
            impls = [(l, p) for l, p in impls if any(l.startswith(x) for x in args.langs)]
        times = {l: [] for l, _ in impls}
        mems = {l: [] for l, _ in impls}
        for rep in range(args.reps):
            for label, p in impls:
                t, base, inc, _, _ = runner.run([p, *group_args(g)])
                times[label].append(t)
                mems[label].append((base, inc))
        ref = statistics.median(times[impls[0][0]]) if impls else 0
        print(f"\n### {g}\n")
        print("| test | time, s | min | memory, MiB | vs Goose |")
        print("|---|---:|---:|---:|---:|")
        for label, _ in impls:
            med = statistics.median(times[label])
            base = statistics.median(m[0] for m in mems[label])
            inc = statistics.median(m[1] for m in mems[label])
            print(f"| {label} | {med:.3f} | {min(times[label]):.3f} | {base:.2f} + {inc:.2f} | {med / ref:.2f} |")
        sys.stdout.flush()


def cmd_ab(args):
    """Several builds of one test, interleaved round after round, so that
    two versions of a Goose test (or of the compiler) compare on equal
    terms."""
    pin(args.cpus)
    runner = Runner()
    g = selected([args.group])[0]
    if g == "json":
        need_json()
    times = {e: [] for e in args.exes}
    for rep in range(args.reps):
        for e in args.exes:
            times[e].append(runner.run([e, *group_args(g)])[0])
    ref = statistics.median(times[args.exes[0]])
    print(f"\n### {g}\n")
    print("| build | time, s | min | vs first |")
    print("|---|---:|---:|---:|")
    for e in args.exes:
        med = statistics.median(times[e])
        print(f"| {e} | {med:.3f} | {min(times[e]):.3f} | {med / ref:.3f} |")


def main():
    global BUILD
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", help="where everything is built (default bench/kostya2/build)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("langs", nargs="*", help="goose, c, rust (c builds C and C++); default all")
    b.add_argument("--goose", help="the goose compiler to use instead of this checkout's build")
    b.add_argument("--only", help="Goose tests to build, comma separated (bf, base64, json, json_dom, matmul, primes)")
    c = sub.add_parser("check")
    c.add_argument("--only")
    r = sub.add_parser("run")
    r.add_argument("--only", help="groups, comma separated substrings (json, mandel, ...)")
    r.add_argument("--langs", help="implementations, comma separated prefixes (goose, rust, c++ ...)")
    r.add_argument("--reps", type=int, default=5)
    r.add_argument("--cpus", default=os.environ.get("BENCH_CPUS", "all"), help="CPUs to pin the runs to (e.g. 16-31)")
    a = sub.add_parser("ab")
    a.add_argument("group", help="the group whose arguments the builds run with (bench.b, json, ...)")
    a.add_argument("exes", nargs="+")
    a.add_argument("--reps", type=int, default=10)
    a.add_argument("--cpus", default=os.environ.get("BENCH_CPUS", "all"))
    args = ap.parse_args()
    if args.build_dir:
        BUILD = Path(args.build_dir).resolve()
    BUILD.mkdir(parents=True, exist_ok=True)
    if getattr(args, "only", None):
        args.only = args.only.split(",")
    if getattr(args, "langs", None) and isinstance(args.langs, str):
        args.langs = args.langs.split(",")
    if args.cmd == "build":
        for l in args.langs or ["goose", "c", "rust"]:
            {"goose": build_goose, "c": build_c, "rust": build_rust}[l](args)
    elif args.cmd == "check":
        cmd_check(args)
    elif args.cmd == "ab":
        cmd_ab(args)
    else:
        cmd_run(args)


if __name__ == "__main__":
    main()
