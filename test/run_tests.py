#!/usr/bin/env python3
"""Goose test runner: parses every test file, checks dump/reparse/dump
roundtrips to identical output, typechecks files not marked `parse-only` on
their first line, checks that error tests fail in the right phase, and (when a
C compiler is available) compiles and runs the generated C at -O0 and -O2,
comparing the two runs and any blessed output in expected/<name>.out.
expected/<name>.aborts marks tests whose run is expected to end in a runtime
abort (nonzero exit) after printing their expected stdout.

  python test/run_tests.py [--exe path/to/goose] [--nocgen]
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
import toolchain as tc

HERE = Path(__file__).resolve().parent


def joined(text):
    """Output as it is compared: LF endings (run_capture already folded them)
    and no trailing blank line, which is how the blessed files are stored."""
    return text.rstrip("\n")


def first_line(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.readline()


class Runner:
    def __init__(self, exe):
        self.exe = exe
        self.failures = 0

    def ok(self, what):
        print(f"ok   {what}")

    def fail(self, what, detail=None):
        if detail:
            sys.stdout.write(detail if detail.endswith("\n") else detail + "\n")
        print(f"FAIL {what}")
        self.failures += 1

    def goose(self, *args):
        """The compiler under test, as (exit code, stdout, stderr). Both
        streams are captured rather than shown, so a failing step can print
        what happened without having to run the compiler a second time."""
        return tc.run_capture([self.exe] + [str(a) for a in args])


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", help="the goose compiler to test")
    ap.add_argument("--nocgen", action="store_true",
                    help="skip everything that needs a C compiler")
    args = ap.parse_args()

    tc.setup_console()
    exe = tc.find_goose(args.exe)
    r = Runner(exe)
    builddir = tc.REPO_ROOT / "build"
    builddir.mkdir(parents=True, exist_ok=True)

    code, out, err = r.goose("--tokens", HERE / "lexer_tokens.goose")
    if code != 0:
        r.fail("lex lexer_tokens.goose")
    else:
        r.ok("lex lexer_tokens.goose")

    tests = [f for f in sorted(HERE.glob("*.goose")) if f.name != "lexer_tokens.goose"]

    for f in tests:
        code, out, err = r.goose("--parse", f)
        if code != 0:
            r.fail(f"parse {f.name}", out + err)
            continue
        code, d1, err = r.goose("--dump", f)
        d1 = joined(d1)
        tmp = builddir / "roundtrip.goose"
        tc.write_text(tmp, d1)
        code, d2, err = r.goose("--dump", tmp)
        d2 = joined(d2)
        if code != 0:
            r.fail(f"reparse-of-dump {f.name}", d2 + err)
        elif d1 != d2:
            r.fail(f"roundtrip {f.name}")
        else:
            r.ok(f"parse+roundtrip {f.name}")
        if "parse-only" not in first_line(f):
            code, out, err = r.goose("--check", f)
            if code != 0:
                r.fail(f"typecheck {f.name}", out + err)
            else:
                r.ok(f"typecheck {f.name}")

    # The optimizer runs at -O1 in every typecheck above; also exercise the
    # other levels (and the --specs dump path) on the optimizer coverage file.
    for lvl in ("-O0", "-O1", "-O2"):
        code, out, err = r.goose(lvl, "--check", "--specs", HERE / "optimize.goose")
        if code != 0:
            r.fail(f"optimize {lvl}", out + err)
        else:
            r.ok(f"optimize {lvl}")

    # Bounds-check elimination: verify the per-line elide/keep annotations in
    # bce.goose (the file's runtime behavior is covered by the cgen runs below).
    code, out, err = r.goose("--check", "--bce-test", HERE / "bce.goose")
    if code != 0:
        r.fail("bce-test bce.goose", out + err)
    else:
        r.ok("bce-test bce.goose")

    # --- codegen: generate C, compile, run, compare ------------------------
    cc = None if args.nocgen else next(iter(tc.find_ccs().values()), None)
    if not cc:
        print("skip codegen run tests (no C compiler found or --nocgen)")
    else:
        gendir = builddir / "gen"
        gendir.mkdir(parents=True, exist_ok=True)
        for f in tests:
            if "parse-only" in first_line(f):
                continue
            name = f.stem
            aborts = (HERE / "expected" / f"{name}.aborts").exists()
            runs, bad = {}, False
            for ol in ("0", "2"):
                cfile = gendir / f"{name}-O{ol}.c"
                efile = gendir / f"{name}-O{ol}{tc.EXE_SUFFIX}"
                code, out, err = r.goose(f"-O{ol}", "-o", cfile, f)
                if code != 0:
                    r.fail(f"cgen -O{ol} {f.name}", out + err)
                    bad = True
                    continue
                ok, log = cc.compile(cfile, efile, log=gendir / f"{name}-O{ol}.cc.log")
                if not ok:
                    r.fail(f"cc -O{ol} {f.name}", "\n".join(log.splitlines()[:8]))
                    bad = True
                    continue
                code, out, err = tc.run_capture([efile])
                if (aborts and code == 0) or (not aborts and code != 0):
                    r.fail(f"run -O{ol} {f.name} (exit {code})",
                           "\n".join(err.splitlines()[:3]))
                    bad = True
                    continue
                runs[ol] = joined(out)
            if bad:
                continue
            if runs["0"] != runs["2"]:
                r.fail(f"cgen-output-differs-by-O {f.name}")
                continue
            expfile = HERE / "expected" / f"{name}.out"
            if expfile.exists():
                want = joined(tc.decode(expfile.read_bytes()))
                if runs["0"] != want:
                    r.fail(f"cgen-expected {f.name}",
                           f"--- got:\n{runs['0']}\n--- want:\n{want}")
                    continue
            r.ok(f"cgen+run {f.name}")

        # One debug-checked build (-DGS_DEBUG=1: overflow and `as` range
        # aborts, 9.3) of the codegen coverage test; its output must not change.
        want = joined(tc.decode((HERE / "expected" / "codegen_exec.out").read_bytes()))
        r.goose("-O0", "-o", gendir / "cgdbg.c", HERE / "codegen_exec.goose")
        ok, log = cc.compile(gendir / "cgdbg.c", gendir / f"cgdbg{tc.EXE_SUFFIX}",
                             defines=["GS_DEBUG=1"], log=gendir / "cgdbg.cc.log")
        if not ok:
            r.fail("cgen-debug-cc codegen_exec.goose", "\n".join(log.splitlines()[:8]))
        else:
            code, out, err = tc.run_capture([gendir / f"cgdbg{tc.EXE_SUFFIX}"])
            if code != 0 or joined(out) != want:
                r.fail(f"cgen-debug codegen_exec.goose (exit {code})")
            else:
                r.ok("cgen-debug codegen_exec.goose")

        # The same coverage test through clang, release and debug. A compiler
        # that accepts more C than the standard does is not what checks the
        # generated C is actually valid: a call to a function defined only in
        # debug builds compiled silently under MSVC and broke every clang
        # release build.
        clang = tc.find_clang_c()
        if not clang:
            print("skip cgen-clang (no clang found)")
        else:
            src = gendir / "cgclang.c"
            r.goose("-O2", "-o", src, HERE / "codegen_exec.goose")
            for label in ("release", "debug"):
                out_exe = gendir / f"cgclang-{label}{tc.EXE_SUFFIX}"
                ok, log = clang.compile(src, out_exe, opt=1, warn="off", strict_decls=True,
                                        defines=["GS_DEBUG=1"] if label == "debug" else [],
                                        log=gendir / f"cgclang-{label}.log")
                if not ok:
                    r.fail(f"cgen-clang-{label} codegen_exec.goose",
                           "\n".join(log.splitlines()[:8]))
                    continue
                code, out, err = tc.run_capture([out_exe])
                if code != 0 or joined(out) != want:
                    r.fail(f"cgen-clang-{label}-output codegen_exec.goose (exit {code})")
                else:
                    r.ok(f"cgen-clang-{label} codegen_exec.goose")

    for f in sorted((HERE / "errors").glob("*.goose")):
        code, out, err = r.goose("--parse", f)
        if code == 0:
            r.fail(f"expected-error {f.name}")
        else:
            r.ok(f"error {f.name}")

    # Typecheck error tests: must parse, must fail the typechecker.
    for f in sorted((HERE / "errors_tc").glob("*.goose")):
        code, out, err = r.goose("--parse", f)
        if code != 0:
            r.fail(f"tc-error-parses {f.name}", out + err)
            continue
        code, out, err = r.goose(f)
        if code == 0:
            r.fail(f"expected-tc-error {f.name}")
        else:
            r.ok(f"tc-error {f.name}")

    # The samples: compiled, built, run and compared with their expected output
    # (or only typechecked without a C compiler), by their own runner.
    sargs = [sys.executable, str(tc.REPO_ROOT / "samples" / "run_samples.py"),
             "--exe", str(exe)]
    if args.nocgen:
        sargs.append("--nocgen")
    if subprocess.run(sargs).returncode != 0:
        r.failures += 1

    if r.failures:
        print(f"{r.failures} FAILURE(S)")
        return 1
    print("all tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
