#!/usr/bin/env python3
"""Goose test runner: parses every test file, checks dump/reparse/dump
roundtrips to identical output, typechecks files not marked `parse-only` on
their first line, checks that error tests fail in the right phase, and (when a
C compiler is available) compiles and runs the generated C at -O0 and -O2,
comparing the two runs and required output in expected/<name>.out.
expected/<name>.aborts marks tests whose run is expected to end in a runtime
abort (nonzero exit) after printing their expected stdout. Each nonblank line
in expected/<name>.stderr must occur in stderr, so an unrelated crash cannot
satisfy an expected abort. Compiler error fixtures require `// error:`
diagnostic substrings in their source and the compiler's normal error exit.
A first-line `runtime-debug` marker adds a targeted
GS_DEBUG=1 run, alongside the existing codegen_exec debug coverage.
A first-line `dump-runtime` marker also executes the parser's dump, checking
that stable roundtripping preserved the original program's behavior.

A compiler built with the TinyCC backend runs the same programs a second way,
in JIT mode: no C file and no external compiler, the generated C built and run
inside the compiler process. Those runs are compared with the same blessed
outputs. A first-line `no-jit` marker leaves a test out of them, and a program
the backend refuses outright is counted as a skip, not a failure.

The gfx/ tests use the SDL3 graphics module. They always parse, typecheck and
generate C; they build and run where the compiler has the gfx layer built in,
linking what `goose --gfx-link` names, and a machine without a GPU device
counts as a skip. A gfx/ fixture with `// error:` markers is a rejection
test, as in errors_tc/. test/gfx_api_check.py checks stdlib/gfx.goose
against the C layer's own list of its functions, structs and constants.

Profiles keep the CI coverage deliberate: baseline compares Goose/native C
-O0 and -O2 plus the targeted debug-runtime runs; sanitize uses Goose -O2 and
Clang -O1 with ASan/UBSan on Linux, including the samples and C runtime tests.

  python test/run_tests.py [--exe path/to/goose] [--nocgen] [--no-jit]
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "scripts"))
import toolchain as tc
import gfx_api_check


def joined(text):
    """Output as it is compared: LF endings (run_capture already folded them)
    and no trailing blank line, which is how the blessed files are stored."""
    return text.rstrip("\n")


def first_line(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.readline()


def error_markers(path):
    return re.findall(r"^// error: (.+)$", path.read_text(encoding="utf-8"), re.MULTILINE)


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
        result = tc.run_capture([self.exe] + [str(a) for a in args])
        if tc.sanitizer_failure(result[2]):
            self.fail(f"compiler sanitizer {args[-1]}", result[2])
        return result

    def run_expected(self, argv, name, label):
        """Run a program and validate it. `argv` is the built executable, or
        the compiler running the program in JIT mode; in both cases stdout is
        the program's alone, since the compiler's own progress lines move to
        stderr when it runs a program."""
        return self.check_run(name, label, *tc.run_capture([str(a) for a in argv]))

    def check_run(self, name, label, code, out, err):
        """Validate termination and diagnostics before comparing stdout."""
        aborts = (HERE / "expected" / f"{name}.aborts").exists()
        if tc.sanitizer_failure(err):
            self.fail(f"sanitizer {label}", err)
            return None
        if (aborts and code == 0) or (not aborts and code != 0):
            self.fail(f"run {label} (exit {code})", err)
            return None
        if not self.check_stderr(name, label, err, required=aborts):
            return None
        return joined(out)

    def check_stderr(self, name, label, err, required=False):
        stderr_file = HERE / "expected" / f"{name}.stderr"
        if required and not stderr_file.exists():
            self.fail(f"missing expected-stderr {label}")
            return False
        if stderr_file.exists():
            markers = [line.strip() for line in tc.decode(stderr_file.read_bytes()).splitlines()
                       if line.strip()]
            if not markers or any(marker not in err for marker in markers):
                self.fail(f"expected-stderr {label}", err)
                return False
        return True

    def check_stdout(self, name, label, out):
        expfile = HERE / "expected" / f"{name}.out"
        if not expfile.exists():
            self.fail(f"missing expected-output {label}")
            return False
        want = joined(tc.decode(expfile.read_bytes()))
        if out != want:
            self.fail(f"expected-output {label}", f"--- got:\n{out}\n--- want:\n{want}")
            return False
        return True

    def check_error(self, path, label, code, out, err):
        # CompileError exits with 1. Signals, access violations and assertion
        # failures are compiler bugs, even if they printed a matching message.
        if code != 1 or tc.sanitizer_failure(err):
            self.fail(f"{label} {path.name} (exit {code})", out + err)
            return False
        markers = error_markers(path)
        # Match diagnostic headers, not echoed source (which may itself name
        # the expected message). The missing-main error has no source location.
        diagnostics = []
        for line in err.splitlines():
            match = re.match(r"^\S.*:\d+(?::\d+)?: error: (.*)$", line)
            if match:
                diagnostics.append(match[1])
            elif line == "program needs exactly one global fn main()":
                diagnostics.append(line)
        diagnostics = "\n".join(diagnostics)
        missing = [m for m in markers if m not in diagnostics]
        if not markers or missing:
            self.fail(f"expected-diagnostic {path.name}",
                      f"missing: {missing or ['// error: marker']}\n{err}")
            return False
        return True

    def roundtrip(self, path, tmp):
        code, d1, err = self.goose("--dump", path)
        if code != 0:
            self.fail(f"dump {path.name}", d1 + err)
            return False
        tc.write_text(tmp, d1)
        code, d2, err = self.goose("--dump", tmp)
        if code != 0:
            self.fail(f"reparse-of-dump {path.name}", d2 + err)
        elif joined(d1) != joined(d2):
            self.fail(f"roundtrip {path.name}")
        else:
            self.ok(f"parse+roundtrip {path.name}")
            return True
        return False

    def check_optimizer(self, level, specs):
        # Observe the transformed bodies, rather than accepting a successful
        # --specs command or pinning unstable specialization IDs/pass counts.
        bodies = {}
        for spec in specs.split("// spec ")[1:]:
            match = re.search(r"^fn (tre_\w+)\([^\n]*\) \{\n", spec, re.MULTILINE)
            if match:
                bodies[match[1]] = spec[match.end():]
        optimized = level != "-O0"
        shapes = {
            "tre_add": (optimized, not optimized),
            "tre_rev": (optimized, not optimized),
            "tre_plain": (optimized, not optimized),
            "tre_mixed": (optimized, True),
            "tre_mod": (False, True),
            "tre_flt": (False, True),
            "tre_from": (False, True),
            "tre_inloop": (False, True),
        }
        valid = True
        for name, want in shapes.items():
            body = bodies.get(name)
            got = None if body is None else (
                bool(re.search(r"\bloop \{", body)),
                bool(re.search(rf"\b{name}\(", body)),
            )
            if got != want:
                self.fail(f"tail-recursion {level} {name}",
                          f"(loop, self call): got {got}, want {want}")
                valid = False
        return valid


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", help="the goose compiler to test")
    ap.add_argument("--nocgen", action="store_true",
                    help="skip everything that needs a C compiler")
    ap.add_argument("--profile", choices=("baseline", "sanitize"), default="baseline",
                    help="baseline: native O0/O2; sanitize: Linux Clang ASan/UBSan at O1")
    ap.add_argument("--cc", choices=("native", "clang", "gcc", "msvc"),
                    help="require this C toolchain instead of optional auto-discovery")
    ap.add_argument("--require-clang", action="store_true",
                    help="fail if the baseline's second C-front-end check is unavailable")
    ap.add_argument("--no-jit", action="store_true",
                    help="skip the in-process TinyCC runs even where they are available")
    args = ap.parse_args()

    if args.nocgen and (args.cc or args.require_clang or args.profile != "baseline"):
        ap.error("--nocgen cannot be combined with a required toolchain or sanitizer profile")
    if args.profile == "sanitize" and (not sys.platform.startswith("linux") or
                                        args.cc not in (None, "clang")):
        ap.error("the sanitize profile requires Linux and Clang")

    tc.setup_console()
    # The suite opens no windows: a gfx program asking for one draws off
    # screen instead.
    os.environ["GOOSE_GFX_HEADLESS"] = "1"
    exe = tc.find_goose(args.exe)
    cc = None if args.nocgen else tc.test_cc("clang" if args.profile == "sanitize" else args.cc)
    clang = None if args.nocgen or args.profile == "sanitize" else tc.find_clang_c()
    if args.require_clang and not clang:
        ap.error("requested secondary C front end is unavailable: clang")
    extra = tc.SANITIZER_FLAGS if args.profile == "sanitize" else ()
    if args.profile == "sanitize":
        tc.use_sanitizer_suppressions()
    # Not under the sanitizers: the program runs inside the compiler process and
    # is not itself instrumented, and its runtime allocations are still held
    # when the compiler exits, which LeakSanitizer reports against the compiler.
    jit = not args.no_jit and args.profile != "sanitize" and tc.have_jit(exe)
    # What a gfx test program links, empty for a compiler built without the
    # gfx layer: those tests then only generate C.
    gfxlibs = tc.gfx_link(exe, cc) if cc else []
    print(f"profile: {args.profile}; C backend: {cc.desc if cc else 'none'}; "
          f"JIT backend: {'TinyCC' if jit else 'none'}; "
          f"gfx: {'linked' if gfxlibs else 'not built in'}")
    r = Runner(exe)
    builddir = tc.REPO_ROOT / "build"
    builddir.mkdir(parents=True, exist_ok=True)
    dumpdir = builddir / "dump"
    dumpdir.mkdir(parents=True, exist_ok=True)

    code, out, err = r.goose("--tokens", HERE / "syntax" / "lexer_tokens.goose")
    if code != 0:
        r.fail("lex lexer_tokens.goose", out + err)
    elif r.check_stdout("lexer_tokens", "lexer_tokens.goose", joined(out)):
        r.ok("lex lexer_tokens.goose")

    # The shader compiler is built into every compiler, SDL or not: a shader
    # compiles, #include included, and a binding outside SDL_GPU's sets is
    # rejected with the rule it broke.
    code, out, err = r.goose("--compile-shader", HERE / "gfx" / "probe.frag")
    want = "samplers 1, storage textures 0 ro / 0 rw, storage buffers 0 ro / 0 rw, uniform blocks 1 (32 bytes)"
    if code != 0 or want not in out:
        r.fail("compile-shader probe.frag", out + err)
    else:
        r.ok("compile-shader probe.frag")
    code, out, err = r.goose("--compile-shader", HERE / "gfx" / "probe_badset.frag")
    if code != 1 or "sampler 'tex' is in set 0, and must be in set 2" not in err:
        r.fail("compile-shader probe_badset.frag", out + err)
    else:
        r.ok("compile-shader probe_badset.frag")

    # Both sides of the gfx module's C boundary describe it: they must agree.
    problems = gfx_api_check.check()
    if problems:
        r.fail("gfx-api stdlib/gfx.goose against src/gfx/gfx_api.h", "\n".join(problems))
    else:
        r.ok("gfx-api stdlib/gfx.goose against src/gfx/gfx_api.h")

    # One level of category directories; nested syntax/ns and syntax/sub are
    # import fixtures, exercised by their entry programs rather than alone.
    tests = [f for f in sorted(HERE.glob("*/*.goose"))
             if f.parent.name not in ("errors", "errors_tc") and f.name != "lexer_tokens.goose"]
    if len({f.stem for f in tests}) != len(tests):
        ap.error("fixture names must be unique across categories (shared expected/ and build outputs)")
    # A gfx fixture with error markers is a rejection test, kept beside the
    # shaders it rejects.
    gfx_errors = [f for f in tests if f.parent.name == "gfx" and error_markers(f)]
    tests = [f for f in tests if f not in gfx_errors]
    gfx_skipped = []

    dump_tests = []
    for f in tests:
        code, out, err = r.goose("--parse", f)
        if code != 0:
            r.fail(f"parse {f.name}", out + err)
            continue
        tmp = dumpdir / f.name
        if r.roundtrip(f, tmp) and "dump-runtime" in first_line(f):
            dump_tests.append(tmp)
        if "parse-only" not in first_line(f):
            code, out, err = r.goose("--check", f)
            if code != 0:
                r.fail(f"typecheck {f.name}", out + err)
            else:
                r.ok(f"typecheck {f.name}")

    # The optimizer runs at -O1 in every typecheck above; also exercise the
    # other levels (and the --specs dump path) on the optimizer coverage file.
    for lvl in ("-O0", "-O1", "-O2"):
        code, out, err = r.goose(lvl, "--check", "--specs", HERE / "optimizer" / "optimize.goose")
        if code != 0:
            r.fail(f"optimize {lvl}", out + err)
        elif r.check_optimizer(lvl, out):
            r.ok(f"optimize {lvl}")

    # Verify every annotated regression, including the expected-abort cases.
    # These describe the default O1 pass; O0/O2 execution checks semantics.
    for f in tests:
        if not re.search(r"//\s*bce:(?:elide|keep)\b", f.read_text(encoding="utf-8")):
            continue
        code, out, err = r.goose("-O1", "--check", "--bce-test", f)
        if code != 0:
            r.fail(f"bce-test {f.name}", out + err)
        else:
            r.ok(f"bce-test {f.name}")

    # --- codegen: generate C, compile, run, compare ------------------------
    if not cc:
        print("skip codegen run tests (no C compiler found or --nocgen)")
    else:
        gendir = builddir / "gen" / args.profile
        gendir.mkdir(parents=True, exist_ok=True)
        for f in tests:
            if "parse-only" in first_line(f):
                continue
            name = f.stem
            isgfx = f.parent.name == "gfx"
            runs, bad = {}, False
            levels = ("0", "2") if args.profile == "baseline" else ("2",)
            for ol in levels:
                cfile = gendir / f"{name}-O{ol}.c"
                efile = gendir / f"{name}-O{ol}{tc.EXE_SUFFIX}"
                code, out, err = r.goose(f"-O{ol}", "-o", cfile, f)
                if code != 0:
                    r.fail(f"cgen -O{ol} {f.name}", out + err)
                    bad = True
                    continue
                # A gfx program still generates C without the layer; there is
                # just nothing to link it with.
                if isgfx and not gfxlibs:
                    gfx_skipped.append(f.name)
                    bad = True
                    continue
                ok, log = cc.compile(cfile, efile,
                                     opt=int(ol) if args.profile == "baseline" else 1,
                                     extra=extra, strict_decls=True,
                                     libs=gfxlibs if isgfx else (),
                                     log=gendir / f"{name}-O{ol}.cc.log")
                if not ok:
                    r.fail(f"cc -O{ol} {f.name}", "\n".join(log.splitlines()[:8]))
                    bad = True
                    continue
                code, out, err = tc.run_capture([efile])
                if isgfx and tc.GFX_NO_DEVICE in err:
                    gfx_skipped.append(f.name)
                    bad = True
                    break
                out = r.check_run(name, f"-O{ol} {f.name}", code, out, err)
                if out is None:
                    bad = True
                    continue
                runs[ol] = out
            if bad:
                continue
            if len(set(runs.values())) != 1:
                r.fail(f"cgen-output-differs-by-O {f.name}")
                continue
            if not r.check_stdout(name, f.name, runs["2"]):
                continue
            r.ok(f"cgen+run {f.name}")

        # A stable dump can still change grouping and therefore semantics.
        # Execute selected dumped programs against the original expectations.
        for f in dump_tests:
            src = gendir / f"{f.stem}-dump.c"
            out_exe = gendir / f"{f.stem}-dump{tc.EXE_SUFFIX}"
            code, out, err = r.goose("-O2", "-o", src, f)
            if code != 0:
                r.fail(f"cgen-dump {f.name}", out + err)
                continue
            ok, log = cc.compile(src, out_exe, opt=2 if args.profile == "baseline" else 1,
                                 extra=extra, strict_decls=True,
                                 log=gendir / f"{f.stem}-dump.cc.log")
            if not ok:
                r.fail(f"cc-dump {f.name}", log)
                continue
            out = r.run_expected([out_exe], f.stem, f"dump {f.name}")
            if out is not None and r.check_stdout(f.stem, f"dump {f.name}", out):
                r.ok(f"dump+run {f.name}")

        # GS_DEBUG changes language overflow/cast checks, independently of
        # native optimization. Cover its helpers under O2 without multiplying
        # every test by a second runtime mode. Sanitizers run these same
        # selected fixtures, so neither kind of check masks the other.
        debug_tests = [f for f in tests if f.stem == "codegen_exec" or
                       "runtime-debug" in first_line(f)]
        for f in debug_tests:
            name = f.stem
            src = gendir / f"{name}-debug.c"
            out_exe = gendir / f"{name}-debug{tc.EXE_SUFFIX}"
            code, out, err = r.goose("-O2", "-o", src, f)
            if code != 0:
                r.fail(f"cgen-debug {f.name}", out + err)
                continue
            ok, log = cc.compile(src, out_exe, opt=2 if args.profile == "baseline" else 1,
                                 defines=["GS_DEBUG=1"], extra=extra, strict_decls=True,
                                 log=gendir / f"{name}-debug.cc.log")
            if not ok:
                r.fail(f"cgen-debug-cc {f.name}", "\n".join(log.splitlines()[:8]))
                continue
            out = r.run_expected([out_exe], name, f"debug {f.name}")
            if out is not None and r.check_stdout(name, f"debug {f.name}", out):
                r.ok(f"cgen-debug {f.name}")

        # Direct runtime lifecycle checks use small region limits and allocator
        # instrumentation that cannot be expressed by a Goose program. Keep
        # this one focused native test in both profiles.
        name = "runtime_threads_lifecycle"
        src = HERE / "threads" / f"{name}.c"
        out_exe = gendir / f"{name}{tc.EXE_SUFFIX}"
        ok, log = cc.compile(src, out_exe, opt=2 if args.profile == "baseline" else 1,
                             extra=extra, strict_decls=True,
                             log=gendir / f"{name}.cc.log")
        if not ok:
            r.fail(f"runtime-cc {name}", log)
        else:
            out = r.run_expected([out_exe], name, name)
            if out is not None and r.check_stdout(name, name, out):
                r.ok(f"runtime {name}")

        # The same coverage test through clang, release and debug. A compiler
        # that accepts more C than the standard does is not what checks the
        # generated C is actually valid: a call to a function defined only in
        # debug builds compiled silently under MSVC and broke every clang
        # release build.
        if args.profile == "sanitize":
            pass  # The full generated-C suite already ran through Clang.
        elif not clang:
            print("skip cgen-clang (no clang found)")
        else:
            src = gendir / "cgclang.c"
            code, out, err = r.goose("-O2", "-o", src, HERE / "codegen" / "codegen_exec.goose")
            if code != 0:
                r.fail("cgen-clang codegen_exec.goose", out + err)
            for label in ("release", "debug"):
                if code != 0:
                    break
                out_exe = gendir / f"cgclang-{label}{tc.EXE_SUFFIX}"
                ok, log = clang.compile(src, out_exe, opt=1, warn="off", strict_decls=True,
                                        defines=["GS_DEBUG=1"] if label == "debug" else [],
                                        log=gendir / f"cgclang-{label}.log")
                if not ok:
                    r.fail(f"cgen-clang-{label} codegen_exec.goose",
                           "\n".join(log.splitlines()[:8]))
                    continue
                out = r.run_expected([out_exe], "codegen_exec", f"clang-{label} codegen_exec.goose")
                if out is not None and r.check_stdout("codegen_exec", f"clang-{label}", out):
                    r.ok(f"cgen-clang-{label} codegen_exec.goose")

    # --- JIT: the same programs, compiled and run inside the compiler --------
    # No C file, no external toolchain: what this checks is that the generated
    # C is portable enough for a third, very different C implementation, and
    # that a program means the same when TinyCC builds it.
    if not jit:
        print("skip JIT run tests (sanitizer profile, --no-jit, or a compiler built "
              "without the TinyCC backend)")
    else:
        skipped = []
        for f in tests:
            line = first_line(f)
            if "parse-only" in line:
                continue
            if "no-jit" in line:
                skipped.append(f.name)
                continue
            name, runs, bad = f.stem, {}, False
            for ol in ("0", "2"):
                code, out, err = r.goose(f"-O{ol}", "--jit", f)
                # A refusal is the backend saying the program needs something
                # it does not have yet, which is a gap to report, not a failure
                # of this test.
                if code != 0 and tc.JIT_UNSUPPORTED in err:
                    skipped.append(f.name)
                    bad = True
                    break
                if f.parent.name == "gfx" and (tc.GFX_UNAVAILABLE in err or
                                               tc.GFX_NO_DEVICE in err):
                    gfx_skipped.append(f.name)
                    bad = True
                    break
                out = r.check_run(name, f"jit -O{ol} {f.name}", code, out, err)
                if out is None:
                    bad = True
                    continue
                runs[ol] = out
            if bad:
                continue
            if len(set(runs.values())) != 1:
                r.fail(f"jit-output-differs-by-O {f.name}")
            elif r.check_stdout(name, f"jit {f.name}", runs["2"]):
                r.ok(f"jit {f.name}")
        # GS_DEBUG selects the checked arithmetic and cast helpers; -D puts the
        # define into the generated C itself, which is the only command line a
        # JIT run has.
        for f in [f for f in tests if f.stem == "codegen_exec" or
                  "runtime-debug" in first_line(f)]:
            if "no-jit" in first_line(f) or f.name in skipped:
                continue
            out = r.check_run(f.stem, f"jit-debug {f.name}",
                              *r.goose("-O2", "--jit", "-DGS_DEBUG=1", f))
            if out is not None and r.check_stdout(f.stem, f"jit-debug {f.name}", out):
                r.ok(f"jit-debug {f.name}")
        if skipped:
            print(f"skip {len(skipped)} JIT test(s) the backend cannot run yet: "
                  + ", ".join(sorted(set(skipped))))

    for f in sorted((HERE / "errors").glob("*.goose")):
        code, out, err = r.goose("--parse", f)
        if r.check_error(f, "expected-error", code, out, err):
            r.ok(f"error {f.name}")

    if gfx_skipped:
        print(f"skip running {len(set(gfx_skipped))} gfx test(s) (no gfx layer or no GPU "
              f"device): " + ", ".join(sorted(set(gfx_skipped))))

    # Typecheck error tests: must parse, must fail the typechecker.
    for f in sorted((HERE / "errors_tc").glob("*.goose")) + gfx_errors:
        code, out, err = r.goose("--parse", f)
        if code != 0:
            r.fail(f"tc-error-parses {f.name}", out + err)
            continue
        code, out, err = r.goose("--check", f)
        if r.check_error(f, "expected-tc-error", code, out, err):
            r.ok(f"tc-error {f.name}")

    # The samples: compiled, built, run and compared with their expected output
    # (or only typechecked without a C compiler), by their own runner.
    sargs = [sys.executable, str(tc.REPO_ROOT / "samples" / "run_samples.py"),
             "--exe", str(exe)]
    if not jit:
        sargs.append("--no-jit")
    if args.nocgen:
        sargs.append("--nocgen")
    else:
        sargs += ["--profile", args.profile]
        if args.cc:
            sargs += ["--cc", args.cc]
    if subprocess.run(sargs).returncode != 0:
        r.failures += 1

    if r.failures:
        print(f"{r.failures} FAILURE(S)")
        return 1
    print("all tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
