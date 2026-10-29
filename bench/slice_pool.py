"""Compare slice_pool with reusable[] using identical workloads native C, standalone TinyCC and JIT.

python bench/slice_pool.py --exe build-root/Release/goose.exe
The matrix covers append, fragmented reuse and resize, 8/128-byte elements,
and inline/preserved exercise call boundaries at Goose O1/O2 by default. Artifacts include
generated Goose/C, compiler logs, all timing samples, sizes and stack reports.
Times include process startup, initialization, final checksumming and shutdown.
"""
import argparse
import ctypes
import hashlib
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import toolchain as tc
import pool_bench_support as support

MODES = {"append": 0, "fragmented": 1, "resize": 2}


def set_affinity(mask):
    # Child processes inherit the harness's affinity, including benchmark binaries.
    if sys.platform == "win32":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetCurrentProcess.restype = ctypes.c_void_p
        kernel.SetProcessAffinityMask.argtypes = (ctypes.c_void_p, ctypes.c_size_t)
        if not kernel.SetProcessAffinityMask(kernel.GetCurrentProcess(), mask):
            raise ctypes.WinError(ctypes.get_last_error())
    elif hasattr(os, "sched_setaffinity"):
        os.sched_setaffinity(0, {i for i in range(mask.bit_length()) if mask & (1 << i)})
    else:
        raise RuntimeError("CPU affinity is not supported on this platform")


def source_for(template, backend, n, live, width, mode, calls):
    for name, value in dict(N=n, LIVE=live, WIDTH=width, MODE=mode).items():
        template, count = re.subn(rf"(?m)^(let {name} = )\d+",
                                  lambda m: m[1] + str(value), template)
        if count != 1:
            raise RuntimeError(f"Missing benchmark constant {name}")
    if calls == "inline":
        template = template.replace("fn exercise(", "inline fn exercise(")
    if backend == "reusable":
        replacements = {
            "import slice_pool;\n": "",
            "p: slice_pool<Item>&": "p: Item[>..]&",
            "var a = slice_pool<Item> {};": "reusable[] var a: Item[>..] = [];",
            "var b = slice_pool<Item> {};": "reusable[] var b: Item[>..] = [];",
        }
        for old, new in replacements.items():
            if template.count(old) != 1:
                raise RuntimeError(f"Workload conversion no longer matches {old}")
            template = template.replace(old, new)
        template = template.replace(".items", "")
    return template


def generate(exe, path, source, level, calls):
    path.write_text(source, encoding="utf-8")
    cfile = path.with_suffix(".c")
    start = time.perf_counter()
    built = subprocess.run([str(exe), f"-O{level}", "--standalone", "--stacks",
                            "-o", str(cfile), str(path)],
                           capture_output=True, text=True, timeout=120)
    report = built.stdout + built.stderr
    path.with_suffix(".log").write_text(report, encoding="utf-8")
    if built.returncode:
        raise RuntimeError(report)
    # Keep identical call policies for both implementations. At O2 an explicit
    # inline Goose function disappears; at O0 request native inlining instead.
    c = cfile.read_text(encoding="utf-8")
    modifier = "GS_NOINLINE" if calls == "boundary" else "GS_INLINE"
    count = 0
    if calls != "natural":
        c, count = re.subn(r"(?m)^static (gs_u64|uint64_t) (exercise_g\w*\()",
                           rf"static {modifier} \1 \2", c)
    if calls == "boundary" and count < 2:
        raise RuntimeError("exercise call boundary was removed unexpectedly")
    if calls == "boundary" and len(re.findall(r"\bexercise_g\w*\(", c)) < count + 2:
        raise RuntimeError("expected both owners to cross the exercise call boundary")
    cfile.write_text(c, encoding="utf-8")
    return cfile, dict(goose_seconds=time.perf_counter() - start, c_bytes=cfile.stat().st_size, exercise_declarations=count,
                       c_policy=calls, stacks=[s for s in report.splitlines() if s.startswith("main program:")])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--library", type=Path, default=ROOT / "stdlib/slice_pool.goose",
                    help="slice-pool source to snapshot alongside generated workloads")
    ap.add_argument("--previous-library", type=Path,
                    help="also measure an older library source in the same alternating run")
    ap.add_argument("--n", type=int, default=1_000_000, help="reuse allocations, multiple of 4")
    ap.add_argument("--grow-n", type=int, default=262_144, help="append allocations, multiple of 4")
    ap.add_argument("--live", type=int, default=4096)
    ap.add_argument("--reps", type=int, default=7)
    ap.add_argument("--warmups", type=int, default=2)
    ap.add_argument("--affinity", type=lambda s: int(s, 0), help="inherited CPU affinity mask, e.g. 0x4")
    ap.add_argument("--modes", nargs="+", choices=MODES, default=list(MODES))
    ap.add_argument("--widths", nargs="+", type=int, choices=(1, 16), default=[1, 16])
    ap.add_argument("--levels", nargs="+", type=int, choices=(0, 1, 2), default=[1, 2])
    ap.add_argument("--calls", nargs="+", choices=("natural", "inline", "boundary"), default=["natural", "boundary"])
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "slice-pool-bench")
    ap.add_argument("--compilers", nargs="+", choices=("msvc", "clang", "tcc", "jit"),
                    default=["clang", "tcc", "jit"],
                    help="jit measures compilation and execution; tcc measures standalone execution")
    args = ap.parse_args()
    if args.n <= 0 or args.grow_n <= 0 or args.n % 4 or args.grow_n % 4:
        ap.error("--n and --grow-n must be positive multiples of 4")
    if args.live < 64 or args.live & (args.live - 1) or args.reps < 1 or args.warmups < 1:
        ap.error("--live must be a power of two >= 64; --reps and --warmups must be positive")
    if args.affinity is not None:
        if args.affinity <= 0:
            ap.error("--affinity must be a positive CPU mask")
        set_affinity(args.affinity)
    exe = tc.find_goose(args.exe)
    compilers = support.compilers(tc, exe, args.compilers)
    if not compilers:
        raise SystemExit("No native C toolchain available")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "bench/goose/rng.goose", out / "rng.goose")
    library = args.library.read_bytes()
    (out / "slice_pool.goose").write_bytes(library)
    def snapshot_internal(source, destination):
        internal = source.parent / "slice_pool_internal.goose"
        if b"import slice_pool_internal;" not in source.read_bytes():
            return None
        data = internal.read_bytes()
        (destination / internal.name).write_bytes(data)
        return hashlib.sha256(data).hexdigest()
    internal_hash = snapshot_internal(args.library, out)
    previous = None
    if args.previous_library:
        previous = args.previous_library.read_bytes()
        folder = out / "previous"
        folder.mkdir(exist_ok=True)
        (folder / "slice_pool.goose").write_bytes(previous)
        previous_internal_hash = snapshot_internal(args.previous_library, folder)
        shutil.copyfile(ROOT / "bench/goose/rng.goose", folder / "rng.goose")
    template = (ROOT / "bench/goose/slice_pool.goose").read_text(encoding="utf-8")
    results, checksums = [], {}
    metadata = dict(platform=platform.platform(), n=args.n, grow_n=args.grow_n,
                    live=args.live, reps=args.reps, warmups=args.warmups, affinity=args.affinity,
                    executable=str(exe),
                    compiler_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                    compilers=args.compilers,
                    slice_pool_sha256=hashlib.sha256(library).hexdigest(),
                    internal_sha256=internal_hash,
                    workload_sha256=hashlib.sha256(template.encode("utf-8")).hexdigest(), runs=results)
    if previous is not None:
        metadata["previous_slice_pool_sha256"] = hashlib.sha256(previous).hexdigest()
        metadata["previous_internal_sha256"] = previous_internal_hash
    for mode in args.modes:
        n = args.grow_n if mode == "append" else args.n
        for width in args.widths:
            for level in args.levels:
                for calls in args.calls:
                    case = f"{mode}-{width * 8}B-O{level}-{calls}"
                    generated = {}
                    backends = ["reusable", "slice_pool"]
                    if previous is not None:
                        backends.append("previous_slice_pool")
                    for backend in backends:
                        folder = out / "previous" if backend == "previous_slice_pool" else out
                        path = folder / f"{case}-{backend}.goose"
                        source = source_for(template, backend, n, args.live, width, MODES[mode], calls)
                        generated[backend] = generate(exe, path, source, level, calls)
                    for compiler in compilers.values():
                        binaries, timings, metrics, cycles = {}, {}, {}, {}
                        for backend, (cfile, metric) in generated.items():
                            binary = out / f"{case}-{backend}-{compiler.name}{tc.EXE_SUFFIX}"
                            binaries[backend], metrics[backend] = support.prepare(
                                compiler, cfile, binary, exe, level, metric)
                            timings[backend], cycles[backend] = [], []
                        key = (mode, width)
                        # Warm both, then alternate ordering to reduce timing drift.
                        for rep in range(args.reps + args.warmups):
                            order = list(binaries)
                            if len(order) > 2:
                                offset = rep % len(order)
                                order = order[offset:] + order[:offset]
                            if rep % 2:
                                order.reverse()
                            for backend in order:
                                elapsed, checksum, cycle_count = support.measure(binaries[backend])
                                expected = checksums.setdefault(key, checksum)
                                if checksum != expected:
                                    raise RuntimeError(f"Checksum mismatch for {case}, {backend}, {compiler.name}")
                                if rep >= args.warmups:
                                    timings[backend].append(elapsed)
                                    if cycle_count is not None:
                                        cycles[backend].append(cycle_count)
                        medians = {k: statistics.median(v) for k, v in timings.items()}
                        scores = {k: statistics.median(v) for k, v in cycles.items()} if all(cycles.values()) else medians
                        row = dict(case=case, compiler=compiler.desc, mode=mode,
                                   item_bytes=width * 8, goose_level=level, calls=calls,
                                   checksum=checksums[key].strip(), seconds=timings,
                                   median_seconds=medians, cycles=cycles,
                                   ratio_metric="cycles" if all(cycles.values()) else "seconds",
                                   ratio=scores["slice_pool"] / scores["reusable"], metrics=metrics)
                        if previous is not None:
                            row["port_ratio"] = scores["slice_pool"] / scores["previous_slice_pool"]
                        results.append(row)
                        (out / "results.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
                        control = (f" previous={medians['previous_slice_pool'] * 1000:.2f} ms "
                                   f"port_ratio={row['port_ratio']:.3f}" if previous is not None else "")
                        print(f"{case:32} {compiler.name:8} reusable={medians['reusable'] * 1000:8.2f} ms "
                              f"slice_pool={medians['slice_pool'] * 1000:8.2f} ms {row['ratio_metric']}_ratio={row['ratio']:.3f}"
                              f"{control}", flush=True)


if __name__ == "__main__":
    main()
