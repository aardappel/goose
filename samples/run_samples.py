#!/usr/bin/env python3
"""Compiles every sample, builds the generated C, runs it and compares its
stdout, byte for byte after newline normalization, with expected/<name>.out.
Samples run with this directory as the working directory; a sample reads
data/<name>.stdin as its stdin when that file exists, and a sample's C header
(<name>.h, see call_c) is passed with --include. Timings and machine-dependent
facts go to stderr, which is not compared.

A compiler built with the TinyCC backend also runs every sample a second way,
in JIT mode -- built and run inside the compiler process, with no C file and no
external toolchain -- and compares that against the same expected output.

Used by test/run_tests.py; runnable on its own:
  python samples/run_samples.py [--exe path/to/goose] [--bless] [--no-jit]
--bless rewrites the expected outputs from the current runs.
"""

import argparse
import re
import sys
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
    args = ap.parse_args()

    if args.nocgen and (args.cc or args.profile != "baseline"):
        ap.error("--nocgen cannot be combined with a required toolchain or sanitizer profile")
    if args.profile == "sanitize" and (not sys.platform.startswith("linux") or
                                        args.cc not in (None, "clang")):
        ap.error("the sanitize profile requires Linux and Clang")

    tc.setup_console()
    exe = tc.find_goose(args.exe)
    cc = None if args.nocgen else tc.test_cc("clang" if args.profile == "sanitize" else args.cc)
    extra = tc.SANITIZER_FLAGS if args.profile == "sanitize" else ()
    # Blessing rewrites the expected outputs from the compiled run, so the
    # JIT comparison against them has nothing to say until that has happened.
    jit = not args.no_jit and not args.bless and tc.have_jit(exe)

    gendir = tc.REPO_ROOT / "build" / "gen" / args.profile / "samples"
    gendir.mkdir(parents=True, exist_ok=True)
    (HERE / "expected").mkdir(exist_ok=True)

    failures, jitskips = 0, []
    for f in sorted(HERE.glob("*.goose")):
        # The number prefix orders the files for reading; outputs, data and
        # headers go by the bare name.
        name = re.sub(r"^\d+_", "", f.stem)
        cfile = gendir / f"{name}.c"
        efile = gendir / (name + tc.EXE_SUFFIX)
        infile = HERE / "data" / f"{name}.stdin"
        expfile = HERE / "expected" / f"{name}.out"
        gargs = ["-O2"]
        header = HERE / f"{name}.h"
        if header.exists():
            gargs += ["--include", str(header)]
        if jit:
            # Same source, same expected output, no C file and no external
            # compiler: the sample built and run inside the compiler process.
            code, out, err = tc.run_capture([exe] + gargs + ["--jit", str(f)], cwd=HERE,
                                            stdin_path=infile if infile.exists() else None)
            if code != 0 and tc.JIT_UNSUPPORTED in err:
                jitskips.append(f.name)
            elif code != 0 or tc.sanitizer_failure(err):
                print("\n".join(err.splitlines()[:3]))
                print(f"FAIL sample-jit {f.name} (exit {code})")
                failures += 1
            else:
                want = normalized(expfile)
                if want is not None and out.rstrip("\n") != want.rstrip("\n"):
                    print(f"FAIL sample-jit-expected {f.name}")
                    print(f"--- got:\n{out}\n--- want:\n{want}")
                    failures += 1
                else:
                    print(f"ok   sample-jit {f.name}")
        gargs += ["-o", str(cfile)] if cc else ["--check"]
        code, out, err = tc.run_capture([exe] + gargs + [str(f)])
        if code != 0:
            sys.stdout.write(out + err)
            print(f"FAIL sample-compile {f.name}")
            failures += 1
            continue
        if not cc:
            print(f"ok   sample-check {f.name}")
            continue
        ok, log = cc.compile(cfile, efile, opt=2 if args.profile == "baseline" else 1,
                             extra=extra, strict_decls=True, log=gendir / f"{name}.cc.log")
        if not ok:
            print("\n".join(log.splitlines()[:8]))
            print(f"FAIL sample-cc {f.name}")
            failures += 1
            continue
        outfile, errfile = gendir / f"{name}.out", gendir / f"{name}.err"
        code, out, err = tc.run_capture([efile], cwd=HERE,
                                        stdin_path=infile if infile.exists() else None)
        tc.write_text(outfile, out)
        tc.write_text(errfile, err)
        if code != 0 or tc.sanitizer_failure(err):
            print("\n".join(err.splitlines()[:3]))
            print(f"FAIL sample-run {f.name} (exit {code})")
            failures += 1
            continue
        got = normalized(outfile)
        if args.bless:
            tc.write_text(expfile, got)
            print(f"ok   sample-blessed {f.name}")
            continue
        want = normalized(expfile)
        if want is not None and got != want:
            print(f"FAIL sample-expected {f.name}")
            print(f"--- got:\n{got}\n--- want:\n{want}")
            failures += 1
            continue
        print(f"ok   sample {f.name}")

    if jitskips:
        print("skip JIT for sample(s) the backend cannot run yet: " + ", ".join(jitskips))
    if failures:
        print(f"{failures} SAMPLE FAILURE(S)")
        return 1
    print("all samples passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
