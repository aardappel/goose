#!/usr/bin/env python3
"""Compiles every sample, builds the generated C, runs it and compares its
stdout, byte for byte after newline normalization, with expected/<name>.out.
Samples run with this directory as the working directory; a sample reads
data/<name>.stdin as its stdin when that file exists, gets the words of
data/<name>.args as its arguments, and a sample's C header (<name>.h, see
call_c) is passed with --include. Timings and machine-dependent facts go to
stderr, which is not compared.

A sample importing gfx draws off screen here (GOOSE_GFX_HEADLESS), links what
`goose --gfx-link` names, and is skipped where the compiler has no gfx layer
or the machine no GPU device. A sample importing audio, physics or ui links what
`goose --audio-link`, `--physics-link` or `--ui-link` names, and is skipped where the
compiler has no such layer.

A compiler built with the TinyCC backend also runs every sample a second way,
in JIT mode -- built and run inside the compiler process, with no C file and no
external toolchain -- and compares that against the same expected output.

Used by test/run_tests.py; runnable on its own:
  python samples/run_samples.py [--exe path/to/goose] [--bless] [--no-jit] [-j N]
--bless rewrites the expected outputs from the current runs. The samples are
worked on --jobs at a time, and their lines print in file order.
"""

import argparse
import concurrent.futures
import contextlib
import os
import re
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
import toolchain as tc

HERE = Path(__file__).resolve().parent


def normalized(path):
    """A file's text with CRLF folded to LF, so the comparison does not depend
    on how the C runtime or git translated line ends."""
    if not path.exists():
        return None
    return tc.decode(path.read_bytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", help="the goose compiler to test")
    ap.add_argument("--bless", action="store_true",
                    help="rewrite expected/*.out from these runs")
    ap.add_argument("--nocgen", action="store_true",
                    help="only typecheck the samples, do not build or run them")
    ap.add_argument("--profile", choices=("baseline", "sanitize"), default="baseline",
                    help="sanitize: require Linux Clang and instrument generated C with ASan/UBSan")
    ap.add_argument("--cc", choices=("native", "clang", "gcc", "msvc"),
                    help="require this C toolchain instead of optional auto-discovery")
    ap.add_argument("--no-jit", action="store_true",
                    help="skip the in-process TinyCC runs even where they are available")
    ap.add_argument("-j", "--jobs", type=int, default=min(os.cpu_count() or 1, 32),
                    help="how many samples are worked on at once; 1 runs them in order")
    ap.add_argument("--gpu-jobs", type=int, default=0,
                    help="how many gfx programs run at once (default: no limit beyond --jobs)")
    args = ap.parse_args()

    if args.nocgen and (args.cc or args.profile != "baseline"):
        ap.error("--nocgen cannot be combined with a required toolchain or sanitizer profile")
    if args.profile == "sanitize" and (not sys.platform.startswith("linux") or
                                        args.cc not in (None, "clang")):
        ap.error("the sanitize profile requires Linux and Clang")

    tc.setup_console()
    # A windowed sample opens no window while it is being tested.
    os.environ["GOOSE_GFX_HEADLESS"] = "1"
    exe = tc.find_goose(args.exe)
    cc = None if args.nocgen else tc.test_cc("clang" if args.profile == "sanitize" else args.cc)
    extra = tc.SANITIZER_FLAGS if args.profile == "sanitize" else ()
    if args.profile == "sanitize":
        tc.use_sanitizer_suppressions()
    # Blessing rewrites the expected outputs from the compiled run, so the
    # JIT comparison against them has nothing to say until that has happened.
    jit = not args.no_jit and not args.bless and tc.have_jit(exe)

    gendir = tc.REPO_ROOT / "build" / "gen" / args.profile / "samples"
    gendir.mkdir(parents=True, exist_ok=True)
    runtime = tc.GooseRuntime(exe, gendir / "runtime")
    (HERE / "expected").mkdir(exist_ok=True)

    native = {"audio": tc.audio_link(exe, cc) if cc else [],
              "gfx": tc.gfx_link(exe, cc) if cc else [],
              "physics": tc.physics_link(exe, cc) if cc else [],
              "ui": tc.ui_link(exe, cc) if cc else []}
    gpulock = threading.BoundedSemaphore(args.gpu_jobs) if args.gpu_jobs else None

    def gpu(modules):
        return gpulock if gpulock and "gfx" in modules else contextlib.nullcontext()

    def sample(f):
        """Everything done with one sample, its two runs one after the other:
        a sample may write a file in this directory. Returns what it printed,
        its failures, and the names of the samples skipped."""
        lines, failures, jitskips, nativeskips = [], 0, [], []
        say = lines.append
        # The number prefix orders the files for reading; outputs, data and
        # headers go by the bare name.
        name = re.sub(r"^\d+_", "", f.stem)
        cfile = gendir / f"{name}.c"
        efile = gendir / (name + tc.EXE_SUFFIX)
        infile = HERE / "data" / f"{name}.stdin"
        argfile = HERE / "data" / f"{name}.args"
        progargs = argfile.read_text(encoding="utf-8").split() if argfile.exists() else []
        # The native modules the sample imports, whose layers it links.
        modules = tc.native_imports(f.read_text(encoding="utf-8"))
        libs = [lib for m in modules for lib in native[m]]
        expfile = HERE / "expected" / f"{name}.out"
        gargs = ["-O2"]
        header = HERE / f"{name}.h"
        if header.exists():
            gargs += ["--include", str(header)]
        if jit:
            # Same source, same expected output, no C file and no external
            # compiler: the sample built and run inside the compiler process.
            with gpu(modules):
                code, out, err = tc.run_capture([exe] + gargs + ["--jit", str(f), "--"] + progargs,
                                                cwd=HERE,
                                                stdin_path=infile if infile.exists() else None)
            if code != 0 and tc.JIT_UNSUPPORTED in err:
                jitskips.append(f.name)
            elif any(tc.native_unavailable(m, err) for m in modules):
                nativeskips.append(f.name)
            elif code != 0 or tc.sanitizer_failure(err):
                say("\n".join(err.splitlines()[:3]))
                say(f"FAIL sample-jit {f.name} (exit {code})")
                failures += 1
            else:
                want = normalized(expfile)
                if want is not None and out.rstrip("\n") != want.rstrip("\n"):
                    say(f"FAIL sample-jit-expected {f.name}")
                    say(f"--- got:\n{out}\n--- want:\n{want}")
                    failures += 1
                else:
                    say(f"ok   sample-jit {f.name}")
        gargs += ["-o", str(cfile)] if cc else ["--check"]
        code, out, err = tc.run_capture([exe] + gargs + [str(f)])
        if code != 0:
            say((out + err).rstrip("\n"))
            say(f"FAIL sample-compile {f.name}")
            return lines, failures + 1, jitskips, nativeskips
        if not cc:
            say(f"ok   sample-check {f.name}")
            return lines, failures, jitskips, nativeskips
        if any(not native[m] for m in modules):
            nativeskips.append(f.name)
            return lines, failures, jitskips, nativeskips
        ok, log = cc.compile(cfile, efile, opt=2 if args.profile == "baseline" else 1,
                             extra=extra, strict_decls=True, libs=libs, runtime=runtime,
                             log=gendir / f"{name}.cc.log")
        if not ok:
            say("\n".join(log.splitlines()[:8]))
            say(f"FAIL sample-cc {f.name}")
            return lines, failures + 1, jitskips, nativeskips
        outfile, errfile = gendir / f"{name}.out", gendir / f"{name}.err"
        with gpu(modules):
            code, out, err = tc.run_capture([efile] + progargs, cwd=HERE,
                                            stdin_path=infile if infile.exists() else None)
        tc.write_text(outfile, out)
        tc.write_text(errfile, err)
        if "gfx" in modules and tc.GFX_NO_DEVICE in err:
            nativeskips.append(f.name)
            return lines, failures, jitskips, nativeskips
        if code != 0 or tc.sanitizer_failure(err):
            say("\n".join(err.splitlines()[:3]))
            say(f"FAIL sample-run {f.name} (exit {code})")
            return lines, failures + 1, jitskips, nativeskips
        got = normalized(outfile)
        if args.bless:
            tc.write_text(expfile, got)
            say(f"ok   sample-blessed {f.name}")
            return lines, failures, jitskips, nativeskips
        want = normalized(expfile)
        if want is not None and got != want:
            say(f"FAIL sample-expected {f.name}")
            say(f"--- got:\n{got}\n--- want:\n{want}")
            return lines, failures + 1, jitskips, nativeskips
        say(f"ok   sample {f.name}")
        return lines, failures, jitskips, nativeskips

    # Each sample's lines print as soon as it and the samples before it are
    # done, in the same order whatever the number of jobs.
    files = sorted(HERE.glob("*.goose"))
    if args.jobs > 1:
        pool = concurrent.futures.ThreadPoolExecutor(args.jobs)
        results = pool.map(sample, files)
    else:
        results = map(sample, files)
    failures, jitskips, nativeskips = 0, [], []
    for lines, fails, jskips, nskips in results:
        if lines:
            print("\n".join(lines), flush=True)
        failures += fails
        jitskips += jskips
        nativeskips += nskips

    if jitskips:
        print("skip JIT for sample(s) the backend cannot run yet: " + ", ".join(jitskips))
    if nativeskips:
        print("skip audio, gfx, physics or ui sample(s) (no layer for them, or no GPU device): " +
              ", ".join(sorted(set(nativeskips))))
    if failures:
        print(f"{failures} SAMPLE FAILURE(S)")
        return 1
    print("all samples passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
