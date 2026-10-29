"""Compare committed builtin pool users with their current slot_pool migrations.

Builtin and current pool versions use the same Goose compiler and Clang C O2.
An optional previous pool binary measures compiler changes on identical source.
Each output must agree; sources, C, build logs and timing samples are retained.
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
from pathlib import Path
import pool_bench_support as support

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import toolchain as tc

WORKLOADS = {
    "lru_indices": ("bench/goose/lru_indices.goose", []),
    "lru_nonopt": ("bench/goose/lru_nonopt.goose", []),
    "plb_lru_1": ("bench/plb/lru/1.goose", ["1000", "3000000"]),
    "plb_lru_2": ("bench/plb/lru/2.goose", ["1000", "3000000"]),
    "merkletrees": ("bench/plb/merkletrees/1.goose", ["17"]),
}


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pin_cpus(spec):
    if not spec:
        return
    cpus = set()
    for part in spec.split(","):
        lo, _, hi = part.partition("-")
        cpus.update(range(int(lo), int(hi or lo) + 1))
    if not cpus or min(cpus) < 0:
        raise ValueError("expected CPU indices or ranges, such as 16-31")
    if os.name == "nt":
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.GetCurrentProcess.restype = ctypes.c_void_p
        mask = sum(1 << c for c in cpus)
        if mask >= 1 << (ctypes.sizeof(ctypes.c_size_t) * 8):
            raise ValueError("CPU mask exceeds the current processor group")
        if not k32.SetProcessAffinityMask(ctypes.c_void_p(k32.GetCurrentProcess()), ctypes.c_size_t(mask)):
            raise ctypes.WinError(ctypes.get_last_error())
    else:
        os.sched_setaffinity(0, cpus)


def execute(binary, args):
    return support.measure([str(binary), *args])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--out", type=Path, default=ROOT / "build/slot-pool-users")
    ap.add_argument("--reps", type=int, default=7)
    ap.add_argument("--warmups", type=int, default=2)
    ap.add_argument("--copies", type=int, default=3, help="renamed executable copies per sample round")
    ap.add_argument("--cpus", help="CPU indices/ranges inherited by every benchmark process")
    ap.add_argument("--previous", type=Path, help="also time saved slot_pool binaries from this harness")
    ap.add_argument("--only", nargs="+", choices=WORKLOADS, default=list(WORKLOADS))
    args = ap.parse_args()
    if args.reps < 1 or args.warmups < 1 or args.copies < 1:
        ap.error("positive reps and warmups required")
    exe = tc.find_goose(args.exe)
    compilers = tc.find_ccs()
    compiler = compilers.get("clang")
    if compiler is None:
        raise SystemExit("Clang is required for this comparison")
    cflags = ["/clang:-falign-functions=4096" if compiler.style == "msvc"
              else "-falign-functions=4096"]
    pin_cpus(args.cpus)
    out = args.out.resolve()
    previous = None
    if args.previous:
        args.previous = args.previous.resolve()
        if out == args.previous or out in args.previous.parents or args.previous in out.parents:
            ap.error("previous and output directories must be separate")
        previous = json.loads((args.previous / "results.json").read_text(encoding="utf-8"))
        for key, value in [("compiler", compiler.desc), ("goose_level", 2), ("native_level", 2),
                           ("extra_cflags", cflags),
                           ("library_sha256", digest((ROOT / "stdlib/slot_pool.goose").read_bytes()))]:
            if previous.get(key) != value:
                ap.error(f"previous {key} differs; this comparison isolates compiler changes")
    out.mkdir(parents=True, exist_ok=True)
    results = dict(platform=platform.platform(), compiler=compiler.desc,
                   baseline=git("rev-parse", args.baseline).decode().strip(),
                   candidate="working-tree", candidate_head=git("rev-parse", "HEAD").decode().strip(),
                   compiler_sha256=digest(exe.read_bytes()),
                   library_sha256=digest((ROOT / "stdlib/slot_pool.goose").read_bytes()),
                   goose_level=2, native_level=2, reps=args.reps, warmups=args.warmups, cpus=args.cpus,
                   copies=args.copies, extra_cflags=cflags,
                   runs=[])
    if previous:
        results["previous_build"] = {k: v for k, v in previous.items() if k != "runs"}
    for name in args.only:
        path, argv = WORKLOADS[name]
        binaries, hashes, metrics = {}, {}, {}
        for backend in ["reusable", "slot_pool"]:
            folder = out / name / backend
            folder.mkdir(parents=True, exist_ok=True)
            source = git("show", f"{args.baseline}:{path}") if backend == "reusable" else (ROOT / path).read_bytes()
            hashes[backend] = digest(source)
            goose = folder / Path(path).name
            goose.write_bytes(source)
            if path.startswith("bench/goose/"):
                (folder / "rng.goose").write_bytes((ROOT / "bench/goose/rng.goose").read_bytes())
            cfile = goose.with_suffix(".c")
            p = subprocess.run([str(exe), "-O2", "--standalone", "--stacks", "-o", str(cfile), str(goose)],
                               capture_output=True, text=True, timeout=180)
            goose.with_suffix(".log").write_text(p.stdout + p.stderr, encoding="utf-8")
            if p.returncode:
                raise RuntimeError(p.stdout + p.stderr)
            binary = folder / ("run" + tc.EXE_SUFFIX)
            ok, log = compiler.compile(cfile, binary, opt=2, extra=cflags)
            (folder / "cc.log").write_text(log, encoding="utf-8")
            if not ok:
                raise RuntimeError(log)
            binaries[backend] = binary
            metrics[backend] = dict(c_bytes=cfile.stat().st_size, exe_bytes=binary.stat().st_size,
                                    c_sha256=digest(cfile.read_bytes()), exe_sha256=digest(binary.read_bytes()),
                                    stacks=[s for s in (p.stdout + p.stderr).splitlines()
                                            if s.startswith("main program:")])
        if previous:
            row = next(r for r in previous["runs"] if r["workload"] == name)
            if row["args"] != argv or row["source_hashes"]["slot_pool"] != hashes["slot_pool"]:
                raise RuntimeError(f"Previous workload differs: {name}")
            folder = args.previous / name / "slot_pool"
            binary = folder / ("run" + tc.EXE_SUFFIX)
            cfile = folder / Path(path).with_suffix(".c").name
            binaries["previous_slot_pool"] = binary
            hashes["previous_slot_pool"] = row["source_hashes"]["slot_pool"]
            metrics["previous_slot_pool"] = dict(row["metrics"]["slot_pool"],
                c_sha256=digest(cfile.read_bytes()), exe_sha256=digest(binary.read_bytes()))
        timings = {k: [] for k in binaries}
        cycles = {k: [] for k in binaries}
        expected = None
        copies = {}
        for backend, binary in binaries.items():
            copies[backend] = []
            for i in range(args.copies):
                dst = out / name / f"{backend}-copy{i}{tc.EXE_SUFFIX}"
                shutil.copyfile(binary, dst)
                copies[backend].append(dst)
        for sample in range((args.reps + args.warmups) * args.copies):
            rep, copy = divmod(sample, args.copies)
            order = list(binaries)
            if len(order) > 2:
                offset = sample % len(order)
                order = order[offset:] + order[:offset]
            if sample % 2:
                order.reverse()
            for backend in order:
                elapsed, output, count = execute(copies[backend][copy], argv)
                if expected is None:
                    expected = output
                if output != expected:
                    raise RuntimeError(f"Output mismatch: {name}, {backend}")
                if rep >= args.warmups:
                    timings[backend].append(elapsed)
                    if count is not None:
                        cycles[backend].append(count)
        medians = {k: statistics.median(v) for k, v in timings.items()}
        scores = ({k: statistics.median(v) for k, v in cycles.items()}
                  if all(cycles.values()) else medians)
        ratio = scores["slot_pool"] / scores["reusable"]
        row = dict(workload=name, args=argv, source_hashes=hashes, output=expected,
                   seconds=timings, median_seconds=medians, cycles=cycles,
                   ratio_metric="cycles" if all(cycles.values()) else "seconds",
                   ratio=ratio, wall_ratio=medians["slot_pool"] / medians["reusable"], metrics=metrics)
        if previous:
            def normalized_c(path):
                return re.sub(r"^/\* Generated by[^\n]+|^static const char gs_file\d+\[\] = [^\n]+",
                              "", path.read_text(encoding="utf-8"), flags=re.MULTILINE)
            row["relative_to_previous"] = scores["slot_pool"] / scores["previous_slot_pool"]
            row["same_c_as_previous_except_source_paths"] = (
                normalized_c(out / name / "slot_pool" / cfile.name) == normalized_c(cfile))
        results["runs"].append(row)
        (out / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        print(f"{name:16} reusable={medians['reusable'] * 1000:.2f} ms "
              f"slot_pool={medians['slot_pool'] * 1000:.2f} ms {row['ratio_metric']}-ratio={ratio:.3f}"
              + (f" previous={medians['previous_slot_pool'] * 1000:.2f} ms"
                 f" new/previous={scores['slot_pool'] / scores['previous_slot_pool']:.3f}"
                 if previous else ""), flush=True)


if __name__ == "__main__":
    main()
