"""Compare equivalent direct-array and root-owner builders at a real call boundary.

python bench/root_structs.py --exe build-root/Release/goose.exe
Generated sources, executables and JSON results go under build/root-bench.
The fill helper is marked GS_NOINLINE in both C programs; all other native
optimizations remain enabled. This measures descriptor borrowing, not Goose
inlining, and verifies identical checksums before reporting timings.
"""
import argparse
import json
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


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--n", type=int, default=4_000_000)
    ap.add_argument("--reps", type=int, default=7)
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "root-bench")
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "bench" / "goose" / "rng.goose", out / "rng.goose")
    compilers = tc.find_ccs()
    if not compilers:
        raise SystemExit("No native C toolchain available")
    results = []
    expected = None
    for level in (0, 2):
        for name in ("push_ref", "push_root"):
            source = (ROOT / "bench" / "goose" / (name + ".goose")).read_text()
            source = re.sub(r"(?m)^(let N = )\d+", lambda m: m[1] + str(args.n), source)
            goose = out / f"{name}-{level}.goose"
            goose.write_text(source)
            cfile = goose.with_suffix(".c")
            command = [str(exe), f"-O{level}", "--standalone", "--stacks", "-o", str(cfile), str(goose)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=120)
            report = built.stdout + built.stderr
            (out / f"{name}-{level}.log").write_text(report)
            if built.returncode:
                raise RuntimeError(report)
            c = cfile.read_text()
            c, count = re.subn(r"\bstatic void (fill_g\w*\()", r"static GS_NOINLINE void \1", c)
            if count < 2:
                raise RuntimeError("fill helper was unexpectedly removed or inlined")
            cfile.write_text(c)
            for compiler in compilers.values():
                binary = out / f"{name}-{level}-{compiler.name}{tc.EXE_SUFFIX}"
                ok, log = compiler.compile(cfile, binary, opt=2)
                if not ok:
                    raise RuntimeError(log)
                timings = []
                for run in range(args.reps + 1):
                    start = time.perf_counter()
                    p = subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)
                    elapsed = time.perf_counter() - start
                    if p.returncode:
                        raise RuntimeError(p.stdout + p.stderr)
                    if expected is None:
                        expected = p.stdout
                    if p.stdout != expected:
                        raise RuntimeError(f"Checksum mismatch: {name}, {compiler.name}")
                    if run:
                        timings.append(elapsed)
                row = dict(program=name, goose_level=level, compiler=compiler.desc,
                           median_seconds=statistics.median(timings), seconds=timings,
                           c_bytes=cfile.stat().st_size, exe_bytes=binary.stat().st_size,
                           stacks=[s for s in report.splitlines() if s.startswith("main program:")])
                results.append(row)
                print(json.dumps(row), flush=True)
    (out / "results.json").write_text(json.dumps(dict(n=args.n, checksum=expected, runs=results), indent=2))


if __name__ == "__main__":
    main()
