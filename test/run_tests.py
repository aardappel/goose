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
`// warning:` substrings declare the warnings a fixture's typecheck prints,
one warning line each, in any order; a positive fixture without them must
print none. A first-line `runtime-debug` marker adds a targeted
GS_DEBUG=1 run, alongside the existing codegen_exec debug coverage.
A first-line `dump-runtime` marker also executes the parser's dump, checking
that stable roundtripping preserved the original program's behavior.

A compiler built with the TinyCC backend runs the same programs a second way,
in JIT mode: no C file and no external compiler, the generated C built and run
inside the compiler process. Those runs are compared with the same blessed
outputs. A first-line `no-jit` marker leaves a test out of them, and a program
the backend refuses outright is counted as a skip, not a failure.

The audio/ tests use the SDL3 PCM mixer without a device, the gfx/ tests
use the SDL3 graphics module, the physics/ tests the Box3D
physics module and the ui/ tests the Nuklear ui module. They always parse,
typecheck and generate C; they build and run where the compiler has the
native layers they use built in -- their category's, and any other they
import, as a ui test drawing through gfx does -- linking what `goose
--audio-link`, `--gfx-link`, `--physics-link` or `--ui-link` names, and a machine without a
GPU device counts as a skip for gfx. A fixture there with `// error:`
markers is a rejection test, as in errors_tc/. test/api_check.py checks
stdlib/audio.goose, stdlib/gfx.goose, stdlib/physics.goose and stdlib/ui.goose against their C
layers' own lists of functions, structs and constants.

Profiles keep the CI coverage deliberate: baseline compares Goose/native C
-O0 and -O2 plus the targeted debug-runtime runs; sanitize uses Goose -O2 and
Clang -O1 with ASan/UBSan on Linux, including the samples and C runtime tests.
The goose_in_goose/ compiler is one multi-file bootstrap fixture: build three
native generations, require identical stage-2/stage-3 C, and run a self-check
of the stage-2 compiler, which stands for both, beside stage 3's build.
TinyCC also emits the same C when available.

The work runs on --jobs threads, each waiting on the processes it starts, and
the log comes out in the same order whatever the number of jobs: each piece of
work prints into a buffer of its own, shown once everything before it has
been. A fixture's own runs happen one after another, since a program may write
files the next run of it would see. The bootstrap's chains at each level, its
JIT self-compiles and the samples' runner start first, being the longest.
One compiler run per fixture parses, checks the dump roundtrip (--roundtrip)
and typechecks; one per level writes the C and also runs the program through
TinyCC (-o with --jit). -j1 runs everything in order on one thread.

The checking runs go --batch files to one compiler process (goose
--multi-test), and the C of --batch fixtures builds into one executable that
runs any of their programs (CC.compile_programs), each run still a process of
its own; a batch that crashes or does not build leaves its files to runs of
their own, which report the failure as theirs. --batch 1 runs everything one
to a process.

  python test/run_tests.py [--exe path/to/goose] [--nocgen] [--no-jit] [-j N] [--batch N]
"""

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import threading
import time
import traceback
from contextlib import contextmanager
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "scripts"))
import toolchain as tc
import api_check


def native_modules(f, native):
    """The native modules a fixture uses: its category directory's, and any
    other it imports itself."""
    mods = [f.parent.name] if f.parent.name in native else []
    for m in tc.native_imports(f.read_text(encoding="utf-8")):
        if m not in mods:
            mods.append(m)
    return mods


def joined(text):
    """Output as it is compared: LF endings (run_capture already folded them)
    and no trailing blank line, which is how the blessed files are stored."""
    return text.rstrip("\n")


def first_line(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.readline()


def debug_fixture(f, line):
    """Whether a fixture also runs with GS_DEBUG: codegen_exec.goose and
    those whose first line asks for it."""
    return f.stem == "codegen_exec" or "runtime-debug" in line


def has_bce_annotations(f):
    return bool(re.search(r"//\s*bce:(?:elide|keep)\b", f.read_text(encoding="utf-8")))


def front_args(line, bce):
    """The flags of a positive fixture's one parse, roundtrip and typecheck
    run, by its first line and whether it has bce annotations."""
    # The default -O1 is what the bce annotations describe.
    return ["--roundtrip"] + (["--parse"] if "parse-only" in line else
                              ["--check"] + (["--bce-test"] if bce else []))


def runtime_defines(source):
    """The -D a fixture's first line asks every compile of it for."""
    source = Path(source)
    if source.suffix != ".goose" or not source.is_file():
        return []
    return re.findall(r"^// runtime-define: (\w+=\w+)$", first_line(source))


# What `goose --multi-test` ends each file's part of stdout and of stderr
# with, on a line of its own, followed by the file's exit code and name.
MULTIMARK = "==== goose --multi-test: exit"


def split_multi(text, files):
    """A `goose --multi-test` stream cut at each file's mark: (exit code,
    what the file printed) for each file it has the mark of, in order, and
    what follows the last mark. The line break before a mark is the mark's
    own."""
    parts, pos = [], 0
    for f in files:
        m = re.compile(rf"\n{re.escape(MULTIMARK)} (-?\d+) {re.escape(str(f))}\n").search(text, pos)
        if not m:
            break
        parts.append((int(m[1]), text[pos:m.start()]))
        pos = m.end()
    return parts, text[pos:]


def error_markers(path):
    return re.findall(r"^// error: (.+)$", path.read_text(encoding="utf-8"), re.MULTILINE)


def warning_markers(path):
    return re.findall(r"^// warning: (.+)$", path.read_text(encoding="utf-8"), re.MULTILINE)


def unmatched_warnings(markers, warnings):
    """Pairs each marker with a warning line of its own whose message contains
    it, as many as can be paired, and returns the markers and the lines left
    over. `warnings` holds (line, message) pairs. A marker can be part of
    several messages, so taking the first line that fits could leave another
    marker without the only line it fits: this is a maximum bipartite
    matching (augmenting paths). A line repeating an earlier one is offered
    last, so that it is the one left over when a warning prints twice."""
    lines = [line for line, _ in warnings]
    order = sorted(range(len(lines)), key=lambda w: lines[w] in lines[:w])
    owner = [None] * len(lines)

    def place(m, tried):
        for w in order:
            if w not in tried and markers[m] in warnings[w][1]:
                tried.add(w)
                if owner[w] is None or place(owner[w], tried):
                    owner[w] = m
                    return True
        return False

    missing = [marker for m, marker in enumerate(markers) if not place(m, set())]
    extra = [line for w, line in enumerate(lines) if owner[w] is None]
    return missing, extra


def call_chain(depth, nest=0):
    """A program whose compile-time call path is `depth` calls long: distinct
    functions, each calling the next from inside `nest` blocks."""
    fns = []
    for i in range(depth):
        call = f"step{i + 1}(a[0], n - 1) + 1"
        for _ in range(nest):
            call = f"{{ {call} }}"
        fns.append(f"fn step{i}(x: i64, n: i64) -> i64 {{\n"
                   f"    if n == 0 {{ return 0; }}\n"
                   f"    let a: i64[1] = [x];\n"
                   f"    {call}\n"
                   f"}}\n")
    fns.append(f"fn step{depth}(x: i64, n: i64) -> i64 {{ n }}\n")
    fns.append("fn main() { print(step0(1, 3)); }\n")
    return "".join(fns)


def guard_runs(guards, links):
    """A program of `guards` guards in a row, in a function's body and in a
    loop's, and of a chain of `links` single-use functions, each calling the
    next behind four guards. It prints `guards - 1`, -1, 5 and 3."""
    lines = ["fn many(x: i64) -> i64 {"]
    lines += [f"    guard x != {i} else {{ return {i}; }}" for i in range(guards)]
    lines += ["    -1", "}",
              "fn skips(n: i64) -> i64 {", "    var hits = 0;", "    for x in n {"]
    lines += [f"        guard x != {i} else {{ continue; }}" for i in range(guards)]
    lines += ["        hits++;", "    }", "    hits", "}"]
    for i in range(links):
        lines += [f"fn link{i}(x: i64, n: i64) -> i64 {{",
                  "    guard n != 0 else { return 0; }",
                  "    guard x != -1 else { return -1; }",
                  "    guard x != -2 else { return -2; }",
                  "    guard x != -3 else { return -3; }",
                  "    let a: i64[1] = [x];",
                  f"    link{i + 1}(a[0], n - 1) + 1",
                  "}"]
    lines.append(f"fn link{links}(x: i64, n: i64) -> i64 {{ n }}")
    lines.append(f'fn main() {{ print(many({guards - 1}), " ", many(-1), " ", '
                 f'skips({guards + 5}), " ", link0(1, 3)); }}')
    return "\n".join(lines) + "\n"


class Out:
    """What one piece of the suite printed and how many failures it counted,
    held until the pieces before it have printed theirs. `value` is what the
    piece hands on to a later one."""

    def __init__(self):
        self.text = []
        self.failures = 0
        self.value = None


class Fixture:
    """One positive fixture's results, an Out per section of the log it
    prints in, and the skips it adds to the summaries."""

    def __init__(self):
        self.front, self.bce, self.cgen, self.dump = Out(), Out(), Out(), Out()
        self.debug, self.jit, self.jitdebug = Out(), Out(), Out()
        self.nativeskips, self.jitskips = [], []
        # What fixture() found and generated, for the rest of the fixture.
        self.line, self.dumped = "", False
        # By build_options key, the compiler run that wrote the C and where.
        self.modules, self.jitlevels, self.gen, self.c = [], (), {}, {}


class Runner:
    """Runs the suite's work on a pool of threads, which mostly wait for the
    processes they start, and prints it in a fixed order: each piece of work
    prints into an Out of its own, and `slots` lists, in the order the log
    shows them, the callables that hand the Outs over. A job may wait for a
    job submitted before it, which the pool has started by then, never for
    a later one. With one job everything runs as it is submitted, on this
    thread."""

    def __init__(self, exe, jobs):
        self.exe = exe
        self.failures = 0
        self.local = threading.local()
        self.pool = concurrent.futures.ThreadPoolExecutor(jobs) if jobs > 1 else None
        self.jobs = jobs
        # Files to a `goose --multi-test` run (goose_each); 0 chooses by the
        # number of files and jobs.
        self.batch = 0
        self.fronts = {}
        self.slots = []
        self.gpulock = None
        # What the C that `goose -o` writes links with, built once per
        # configuration by whichever job first asks for it.
        self.runtime = tc.GooseRuntime(exe, tc.REPO_ROOT / "build" / "gen" / "runtime")

    # --- output -----------------------------------------------------------

    def say(self, text):
        """A line of the current piece of work's output, or straight to stdout
        outside one."""
        out = getattr(self.local, "out", None)
        if out is None:
            sys.stdout.write(text + "\n")
        else:
            out.text.append(text + "\n")

    def ok(self, what):
        self.say(f"ok   {what}")

    def fail(self, what, detail=None):
        if detail:
            self.say(detail[:-1] if detail.endswith("\n") else detail)
        self.say(f"FAIL {what}")
        out = getattr(self.local, "out", None)
        if out is None:
            self.failures += 1
        else:
            out.failures += 1

    @contextmanager
    def into(self, out):
        """Prints what runs inside into `out`."""
        prev = getattr(self.local, "out", None)
        self.local.out = out
        try:
            yield out
        finally:
            self.local.out = prev

    def absorb(self, out):
        """Prints what another piece of work printed into `out`, and counts
        its failures, as part of the current one."""
        for line in out.text:
            self.say(line[:-1])
        cur = getattr(self.local, "out", None)
        if cur is None:
            self.failures += out.failures
        else:
            cur.failures += out.failures

    # --- scheduling -------------------------------------------------------

    def submit(self, fn, *args, alone=False):
        """A future of fn(*args), run on the pool, or with `alone` on a
        thread of its own."""
        if self.pool and alone:
            thread = concurrent.futures.ThreadPoolExecutor(1)
            future = thread.submit(fn, *args)
            thread.shutdown(wait=False)
            return future
        if self.pool:
            return self.pool.submit(fn, *args)
        future = concurrent.futures.Future()
        try:
            future.set_result(fn(*args))
        except BaseException as exc:
            future.set_exception(exc)
        return future

    def task(self, fn, *args, alone=False):
        """A future of the Out that fn(*args) prints into, with its result as
        the Out's value."""
        def run():
            out = Out()
            with self.into(out):
                out.value = fn(*args)
            return out
        return self.submit(run, alone=alone)

    def show(self, source):
        """Adds a slot: a future of an Out, or a callable returning one."""
        self.slots.append(source.result if isinstance(source, concurrent.futures.Future)
                          else source)

    def show_task(self, fn, *args):
        future = self.task(fn, *args)
        self.show(future)
        return future

    def show_later(self, fn, *args):
        """Adds a slot that runs fn(*args) on this thread when the log reaches
        it, once everything shown before it is done: for comparisons and
        summaries over the results of earlier pieces."""
        def run():
            out = Out()
            with self.into(out):
                fn(*args)
            return out
        self.slots.append(run)

    def flush(self):
        """Prints the slots in order, each as soon as it and everything before
        it is done, and adds up their failures."""
        for slot in self.slots:
            try:
                out = slot()
            except Exception:
                out = Out()
                out.text.append(traceback.format_exc())
                out.text.append("FAIL runner exception\n")
                out.failures = 1
            sys.stdout.write("".join(out.text))
            sys.stdout.flush()
            self.failures += out.failures
        self.slots = []

    # --- running the compiler and what it built ---------------------------

    def goose(self, *args):
        """The compiler under test, as (exit code, stdout, stderr). Both
        streams are captured rather than shown, so a failing step can print
        what happened without having to run the compiler a second time."""
        return self.screened(args[-1], tc.run_capture(self.goose_argv(args)))

    def goose_argv(self, args):
        # Resource-bound regressions use the same runtime configuration
        # through generated C and TinyCC, at every optimization level.
        return [self.exe] + ["-D" + d for d in runtime_defines(args[-1])] + [str(a) for a in args]

    def screened(self, what, result):
        """A compiler run's result, failing the current piece of work if the
        compiler's sanitizers reported anything."""
        if tc.sanitizer_failure(result[2]):
            self.fail(f"compiler sanitizer {what}", result[2])
        return result

    def goose_each(self, args, files):
        """What goose(*args, f) runs the compiler for, for each of `files`,
        as callables returning its (exit code, stdout, stderr) unscreened:
        the runs happen on the pool, up to `batch` files to one `goose
        --multi-test` (multi_test), and screened() is for whoever uses
        them. A file with runtime defines runs in a plain run of its own,
        as every file does with a `batch` of 1."""
        results = {}
        alone = [f for f in files if self.batch == 1 or runtime_defines(f)]
        together = [f for f in files if f not in alone]
        size = self.batch or max(1, min(32, len(together) // (2 * self.jobs)))
        for i in range(0, len(together), size):
            chunk = together[i:i + size]
            future = self.submit(self.multi_test, args, chunk)
            for f in chunk:
                results[f] = lambda f=f, future=future: future.result()[f]
        for f in alone:
            results[f] = self.submit(tc.run_capture, self.goose_argv([*args, f])).result
        return results

    def multi_test(self, args, files):
        """{f: what `goose *args f` gives} for each of `files`, from one
        `goose --multi-test *args *files` where it got through them: each
        file's part of both streams ends in a line of its own with what the
        file's run would have exited with (MULTIMARK). A run that ends
        before a file's mark, a crash or an abort, gets through the files
        before it; the rest run one to a process, which shows the failure
        as theirs. A batch that exits with an error after every mark, or
        prints anything after the last one (a leak report), gets through
        none."""
        argv = [self.exe, "--multi-test"] + [str(a) for a in args] + [str(f) for f in files]
        code, out, err = tc.run_capture(argv)
        outs, outrest = split_multi(out, files)
        errs, errrest = split_multi(err, files)
        done = {}
        for f, (ocode, o), (ecode, e) in zip(files, outs, errs):
            if ocode != ecode:
                break
            done[f] = (ocode, o, e)
        if len(done) == len(files) and (code != 0 or outrest or errrest):
            done = {}
        for f in files:
            if f not in done:
                done[f] = tc.run_capture(self.goose_argv([*args, f]))
        return done

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
        # Checking stops at the error, so which warnings print before it
        # depends on how far it got: a rejection test answers for them only
        # if it declares them.
        return not warning_markers(path) or self.check_warnings(path, out, err)

    def check_warnings(self, path, out, err):
        """The warnings a check printed are the ones the fixture's `// warning:`
        lines declare: one warning line per marker, the marker part of its
        message, in any order. A fixture without markers prints none."""
        # A diagnostic's echoed source is indented, and a warning at no known
        # place is located at "?".
        warnings = [(match[0], match[1]) for match in
                    (re.match(r"\S.*?: warning: (.*)$", line)
                     for line in out.splitlines() + err.splitlines()) if match]
        missing, extra = unmatched_warnings(warning_markers(path), warnings)
        if missing or extra:
            self.fail(f"expected-warnings {path.name}",
                      "".join(f"no warning for marker: {m}\n" for m in missing) +
                      "".join(f"no marker for warning: {w}\n" for w in extra) + out + err)
            return False
        return True

    # --- one positive fixture ---------------------------------------------

    def fixture(self, f):
        """What the suite does with one positive fixture up to its C: the
        front end's checks, then the C at each level, and the program's
        runs through TinyCC, and the C of its dump and debug programs. The
        rest (fixture_runs) follows in a job of its own once the C is
        built, so that the fixture's runs, which may share files the
        program writes, still happen one after another."""
        res = Fixture()
        res.line = line = first_line(f)
        bce = has_bce_annotations(f)
        with self.into(res.front):
            res.dumped = self.front(f, line, bce, res)
        if "parse-only" in line:
            if bce:
                with self.into(res.bce):
                    code, out, err = self.goose("-O1", "--check", "--bce-test", f)
                    if code != 0:
                        self.fail(f"bce-test {f.name}", out + err)
                    else:
                        self.ok(f"bce-test {f.name}")
            return res
        self.generate(f, line, res)
        if self.cc and res.dumped:
            with self.into(res.dump):
                res.c["dump"] = self.gendir / f"{f.stem}-dump.c"
                res.gen["dump"] = self.goose("-O2", "-o", res.c["dump"], self.dumpdir / f.name)
        if self.cc and debug_fixture(f, line):
            with self.into(res.debug):
                res.c["debug"] = self.gendir / f"{f.stem}-debug.c"
                res.gen["debug"] = self.goose("-O2", "-o", res.c["debug"], f)
        return res

    def fixture_runs(self, f, generated, builds):
        """The rest of what the suite does with a positive fixture, after
        fixture() (`generated`, its future): build and run its C, and its
        dump and debug programs. `builds` has, by build_options key, a
        future of what build_programs built for a batch of fixtures."""
        res = generated.result()
        line = res.line
        if "parse-only" in line:
            return res
        built = {key: b.result().get(f) for key, b in builds.items()}
        self.programs(f, res, built)
        if self.cc and res.dumped:
            with self.into(res.dump):
                self.dump_program(f, res, built.get("dump"))
        debug = debug_fixture(f, line)
        if self.cc and debug:
            with self.into(res.debug):
                self.debug_program(f, res, built.get("debug"))
        if self.jit and debug and "no-jit" not in line and f.name not in res.jitskips:
            with self.into(res.jitdebug):
                out = self.check_run(f.stem, f"jit-debug {f.name}",
                                     *self.goose("-O2", "--jit", "-DGS_DEBUG=1", f))
                if out is not None and self.check_stdout(f.stem, f"jit-debug {f.name}", out):
                    self.ok(f"jit-debug {f.name}")
        return res

    def front(self, f, line, bce, res):
        """Parses, checks the dump/reparse/dump roundtrip, and typechecks with
        the fixture's warnings, in one compiler run that says how far it got.
        Returns whether the dump was written for a `dump-runtime` run."""
        parseonly = "parse-only" in line
        if "dump-runtime" in line:
            dumpfile = self.dumpdir / f.name
            dumpfile.unlink(missing_ok=True)
            code, out, err = self.goose("--dump-file", dumpfile, *front_args(line, bce), f)
        else:
            dumpfile = None
            code, out, err = self.screened(f, self.fronts[f]())
        text = out + err
        if not re.search(r"^parsed ok:", text, re.M):
            self.fail(f"parse {f.name}", text)
            return False
        if not re.search(r"^roundtrip ok:", text, re.M):
            what = "reparse-of-dump" if "the dump does not parse again" in err else "roundtrip"
            self.fail(f"{what} {f.name}", text)
            return False
        self.ok(f"parse+roundtrip {f.name}")
        if parseonly:
            return bool(dumpfile)
        # A failed annotation is reported after a successful typecheck.
        bcefailed = bce and re.search(r"^bce-test: \d+ annotation failure", err, re.M)
        if not re.search(r"^typechecked ok:", text, re.M) or (code != 0 and not bcefailed):
            self.fail(f"typecheck {f.name}", text)
        elif self.check_warnings(f, out, err):
            self.ok(f"typecheck {f.name}")
        if bce:
            with self.into(res.bce):
                if code != 0:
                    self.fail(f"bce-test {f.name}", text)
                else:
                    self.ok(f"bce-test {f.name}")
        return bool(dumpfile)

    def generate(self, f, line, res):
        """Generates C at each level, and runs the program through TinyCC.
        Where both happen, one compiler run writes the C and runs the
        program in-process: --jit with -o."""
        name = f.stem
        res.modules = modules = native_modules(f, self.native)
        levels = self.levels if self.cc else ()
        res.jitlevels = jitlevels = ("0", "2") if self.jit and "no-jit" not in line else ()
        gen, cfiles = res.gen, res.c
        with self.into(res.cgen):
            for ol in sorted(set(levels) | set(jitlevels)):
                args = [f"-O{ol}"]
                if ol in levels:
                    cfiles[ol] = self.gendir / f"{name}-O{ol}.c"
                    cfiles[ol].unlink(missing_ok=True)
                    args += ["-o", cfiles[ol]]
                if ol in jitlevels:
                    args.append("--jit")
                with self.gpu(modules):
                    gen[ol] = self.goose(*args, f)

    def build_options(self, key):
        """How a fixture's C is built: at a level ("0", "2"), its dump's
        ("dump") or its GS_DEBUG build ("debug"). (opt, defines)."""
        if key in ("dump", "debug"):
            return 2 if self.profile == "baseline" else 1, ["GS_DEBUG=1"] if key == "debug" else []
        return int(key) if self.profile == "baseline" else 1, []

    def builds_together(self, res, key):
        """Whether the C a fixture() result wrote for `key` is there to build
        with others': not where it links a native layer, or was not written
        (where a JIT run of it followed, the run has the exit code)."""
        cfile = res.c.get(key)
        if cfile is None or res.modules:
            return False
        return cfile.is_file() if key in res.jitlevels else res.gen[key][0] == 0

    def build_programs(self, key, generated):
        """Builds the programs that the fixture() futures in `generated`
        (fixture: future) wrote the C of for `key` (build_options) into one
        executable (CC.compile_programs), where there are two or more and
        they build; {fixture: the argv that runs its program} for those."""
        fixtures = [f for f, future in generated.items()
                    if self.builds_together(future.result(), key)]
        if len(fixtures) < 2:
            return {}
        exe = self.gendir / "programs" / f"{fixtures[0].stem}-{key}{tc.EXE_SUFFIX}"
        exe.parent.mkdir(parents=True, exist_ok=True)
        opt, defines = self.build_options(key)
        ok, _ = self.cc.compile_programs([generated[f].result().c[key] for f in fixtures], exe,
                                         opt=opt, defines=defines, extra=self.extra,
                                         strict_decls=True, runtime=self.runtime,
                                         log=exe.with_suffix(".cc.log"))
        return {f: [exe, str(k)] for k, f in enumerate(fixtures)} if ok else {}

    def programs(self, f, res, built):
        """Builds and runs the C generate() wrote at each level, where
        `built` has no argv for it from build_programs, on its own, and
        judges the program's TinyCC runs."""
        name = f.stem
        modules = res.modules
        libs = [lib for m in modules for lib in self.native[m]]
        levels = self.levels if self.cc else ()
        jitlevels, gen, cfiles = res.jitlevels, res.gen, res.c
        if levels:
            with self.into(res.cgen):
                runs, bad = {}, False
                for ol in levels:
                    cfile = cfiles[ol]
                    efile = self.gendir / f"{name}-O{ol}{tc.EXE_SUFFIX}"
                    code, out, err = gen[ol]
                    # A run that went on to run the program has its exit code;
                    # the C is there if the compile got that far.
                    if code != 0 if ol not in jitlevels else not cfile.is_file():
                        self.fail(f"cgen -O{ol} {f.name}", out + err)
                        bad = True
                        continue
                    # A program using a native module still generates C without
                    # the layer; there is just nothing to link it with.
                    if any(not self.native[m] for m in modules):
                        res.nativeskips.append(f.name)
                        bad = True
                        continue
                    argv = built.get(ol)
                    if not argv:
                        # A program that did not build in a batch shows here
                        # why, built on its own.
                        argv = [efile]
                        opt, _ = self.build_options(ol)
                        ok, log = self.cc.compile(cfile, efile, opt=opt,
                                                  extra=self.extra, strict_decls=True, libs=libs,
                                                  runtime=self.runtime,
                                                  log=self.gendir / f"{name}-O{ol}.cc.log")
                        if not ok:
                            self.fail(f"cc -O{ol} {f.name}", "\n".join(log.splitlines()[:8]))
                            bad = True
                            continue
                    with self.gpu(modules):
                        code, out, err = tc.run_capture(argv)
                    if "gfx" in modules and tc.GFX_NO_DEVICE in err:
                        res.nativeskips.append(f.name)
                        bad = True
                        break
                    out = self.check_run(name, f"-O{ol} {f.name}", code, out, err)
                    if out is None:
                        bad = True
                        continue
                    runs[ol] = out
                if not bad:
                    if len(set(runs.values())) != 1:
                        self.fail(f"cgen-output-differs-by-O {f.name}")
                    elif self.check_stdout(name, f.name, runs["2"]):
                        self.ok(f"cgen+run {f.name}")

        if not self.jit:
            return
        if not jitlevels:
            res.jitskips.append(f.name)
            return
        with self.into(res.jit):
            runs, bad = {}, False
            for ol in jitlevels:
                code, out, err = gen[ol]
                # A refusal is the backend saying the program needs something
                # it does not have yet, which is a gap to report, not a failure
                # of this test.
                if code != 0 and tc.JIT_UNSUPPORTED in err:
                    res.jitskips.append(f.name)
                    return
                if any(tc.native_unavailable(m, err) for m in modules):
                    res.nativeskips.append(f.name)
                    return
                out = self.check_run(name, f"jit -O{ol} {f.name}", code, out, err)
                if out is None:
                    bad = True
                    continue
                runs[ol] = out
            if bad:
                return
            if len(set(runs.values())) != 1:
                self.fail(f"jit-output-differs-by-O {f.name}")
            elif self.check_stdout(name, f"jit {f.name}", runs["2"]):
                self.ok(f"jit {f.name}")

    def dump_program(self, f, res, argv):
        """A stable dump can still change grouping and therefore semantics:
        the dumped program runs against the original expectations. Its C is
        from fixture(); `argv` runs it where build_programs built it."""
        dumpfile = self.dumpdir / f.name
        code, out, err = res.gen["dump"]
        if code != 0:
            self.fail(f"cgen-dump {dumpfile.name}", out + err)
            return
        if not argv:
            argv = [self.gendir / f"{f.stem}-dump{tc.EXE_SUFFIX}"]
            opt, defines = self.build_options("dump")
            ok, log = self.cc.compile(res.c["dump"], argv[0], opt=opt, defines=defines,
                                      extra=self.extra, strict_decls=True, runtime=self.runtime,
                                      log=self.gendir / f"{f.stem}-dump.cc.log")
            if not ok:
                self.fail(f"cc-dump {dumpfile.name}", log)
                return
        out = self.run_expected(argv, f.stem, f"dump {dumpfile.name}")
        if out is not None and self.check_stdout(f.stem, f"dump {dumpfile.name}", out):
            self.ok(f"dump+run {dumpfile.name}")

    def debug_program(self, f, res, argv):
        """GS_DEBUG changes language overflow/cast checks, independently of
        native optimization: its helpers under O2. The C is from fixture();
        `argv` runs it where build_programs built it."""
        name = f.stem
        code, out, err = res.gen["debug"]
        if code != 0:
            self.fail(f"cgen-debug {f.name}", out + err)
            return
        if not argv:
            argv = [self.gendir / f"{name}-debug{tc.EXE_SUFFIX}"]
            opt, defines = self.build_options("debug")
            ok, log = self.cc.compile(res.c["debug"], argv[0], opt=opt, defines=defines,
                                      extra=self.extra, strict_decls=True, runtime=self.runtime,
                                      log=self.gendir / f"{name}-debug.cc.log")
            if not ok:
                self.fail(f"cgen-debug-cc {f.name}", "\n".join(log.splitlines()[:8]))
                return
        out = self.run_expected(argv, name, f"debug {f.name}")
        if out is not None and self.check_stdout(name, f"debug {f.name}", out):
            self.ok(f"cgen-debug {f.name}")

    @contextmanager
    def gpu(self, modules):
        """Holds the GPU for a program using gfx, where --gpu-jobs limits how
        many of those run at once."""
        if "gfx" not in modules or self.gpulock is None:
            yield
            return
        with self.gpulock:
            yield

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

    # --- the Goose-written compiler ---------------------------------------

    def goose_in_goose(self, cc, profile, extra, jit):
        """Exercise the compiler as a large program, then execute its output.

        Stage 1 comes from the compiler under test; each subsequent stage is
        emitted by the preceding executable from the same Goose sources.
        All native stages use the selected toolchain/profile, including the
        sanitizer flags. These modules are not standalone output fixtures.

        The chain of stages at each level, and the JIT self-compile at each,
        are jobs of their own, started here; what this returns adds their
        slots, and those of the comparisons between them, to the log."""
        source = HERE / "goose_in_goose" / "main.goose"
        directory = tc.REPO_ROOT / "build" / "gen" / profile / "goose_in_goose"

        # Generated compilers need the same stack budget as the host. Only
        # the child's soft limit is raised, by the shell that starts it: the
        # suite process keeps its own, and a preexec_fn is not safe with the
        # suite's threads.
        stack_prefix = []
        if sys.platform.startswith("linux"):
            import resource
            soft, hard = resource.getrlimit(resource.RLIMIT_STACK)
            target = 64 * 1024 * 1024
            if hard != resource.RLIM_INFINITY:
                target = min(target, hard)
            if soft != resource.RLIM_INFINITY and soft < target:
                stack_prefix = ["sh", "-c", f'ulimit -S -s {target // 1024} && exec "$@"', "sh"]

        def run(argv, label, log):
            try:
                # The -O0 chain is the suite's critical path; on Windows its
                # compilers run ahead of the work around them.
                result = subprocess.run(stack_prefix + [str(a) for a in argv], cwd=tc.REPO_ROOT,
                                        capture_output=True, timeout=180,
                                        creationflags=getattr(subprocess,
                                                              "ABOVE_NORMAL_PRIORITY_CLASS", 0))
                code, out, err = result.returncode, tc.decode(result.stdout), tc.decode(result.stderr)
            except subprocess.TimeoutExpired as exc:
                code, out, err = -1, tc.decode(exc.stdout or b""), tc.decode(exc.stderr or b"") + "\n180s timeout"
            except OSError as exc:
                code, out, err = -1, "", str(exc)
            tc.write_text(log, f"exit {code}\n{out}{err}")
            if code != 0 or tc.sanitizer_failure(err):
                self.fail(f"{label} (exit {code}; log: {log})", out + err)
                return None
            self.ok(label)
            return out

        stack_flags = []
        if cc:
            if cc.style == "msvc":
                stack_flags = ["/F67108864"]
            elif tc.IS_WINDOWS:
                # A gcc-style Clang driver can still use the MSVC linker.
                _, target, _ = tc.run_capture([cc.cc, "-dumpmachine"])
                stack_flags = ["-Wl,/STACK:67108864"] if "msvc" in target else ["-Wl,--stack,67108864"]
            elif tc.IS_MACOS:
                stack_flags = ["-Wl,-stack_size,0x4000000"]

        def self_check(compiler, label, log):
            """Repeated compilations also exercise context cleanup in the
            built compiler, without pinning internal counts."""
            out = run([compiler, "--check-many", source, source], f"self-check {label} stage 2", log)
            if out is not None:
                lines = out.splitlines()
                if (len(lines) != 2 or lines[0] != lines[1] or
                        not re.fullmatch(r"checked \d+ specializations, \d+ types", lines[0])):
                    self.fail(f"repeated self-check output {label}", out)

        def chain(ol):
            """The native stages at one level; the stage-2 C, when they all
            built."""
            label = f"goose_in_goose -O{ol}"
            work = directory / f"O{ol}"
            work.mkdir(parents=True, exist_ok=True)
            if not cc:
                run([self.exe, f"-O{ol}", "--check", source], f"typecheck {label}", work / "host.check.log")
                return None
            checks = []
            try:
                return stages(ol, label, work, checks)
            finally:
                for check in checks:
                    self.absorb(check.result())

        def stages(ol, label, work, checks):
            compiler = self.exe
            cfiles = []
            for stage in range(1, 4):
                cfile = work / f"stage{stage}.c"
                executable = work / f"stage{stage}{tc.EXE_SUFFIX}"
                cfile.unlink(missing_ok=True)
                executable.unlink(missing_ok=True)
                flags = [f"-O{ol}"] if stage == 1 else []
                what = f"{label} stage {stage}"
                if run([compiler, *flags, "-o", cfile, source], f"emit {what}",
                       work / f"stage{stage}.emit.log") is None:
                    return None
                if not cfile.is_file() or not cfile.stat().st_size:
                    self.fail(f"missing or empty C output {what}")
                    return None
                # Later stages hold the Goose-written compiler's own runtime.
                ok, log = cc.compile(cfile, executable, opt=ol if profile == "baseline" else 1,
                                     extra=[*extra, *stack_flags], strict_decls=True,
                                     runtime=self.runtime if stage == 1 else None,
                                     log=work / f"stage{stage}.cc.log")
                if not ok:
                    self.fail(f"cc {what}", log)
                    return None
                self.ok(f"cc {what}")
                cfiles.append(cfile)
                compiler = executable
                # Stage 3 is built from the same C as stage 2 (the fixed
                # point below), so stage 2 self-checks for both, while stage 3
                # is emitted and built: the -O0 chain is the suite's critical
                # path.
                if stage == 2:
                    checks.append(self.task(self_check, executable, label,
                                            work / "stage2.check.log", alone=True))
            native_c = cfiles[1].read_bytes()
            if native_c != cfiles[2].read_bytes():
                self.fail(f"fixed point {label}", f"C differs: {cfiles[1]} vs {cfiles[2]}")
            else:
                self.ok(f"fixed point {label} ({len(native_c)} bytes)")
            return native_c

        def jit_self_compile(ol):
            """The Goose-written compiler run through TinyCC, compiling
            itself; the C it wrote."""
            label = f"goose_in_goose -O{ol}"
            work = directory / f"O{ol}"
            work.mkdir(parents=True, exist_ok=True)
            cfile = work / "jit-stage2.c"
            cfile.unlink(missing_ok=True)
            out = run([self.exe, f"-O{ol}", "--jit", source, "--", "-o", cfile, source],
                      f"JIT self-compile {label}", work / "jit.emit.log")
            if out is None:
                return None
            if not cfile.is_file() or not cfile.stat().st_size:
                self.fail(f"missing or empty JIT C output {label}")
                return None
            return cfile.read_bytes()

        levels = (0, 2) if profile == "baseline" else (2,)
        chains = {ol: self.task(chain, ol) for ol in levels}
        jits = {ol: self.task(jit_self_compile, ol) for ol in levels} if jit else {}

        def compare_jit(ol):
            jit_c, native_c = jits[ol].result().value, chains[ol].result().value
            if jit_c is not None and native_c is not None:
                if jit_c != native_c:
                    self.fail(f"JIT/native self-compile differs goose_in_goose -O{ol}")
                else:
                    self.ok(f"JIT/native self-compile matches goose_in_goose -O{ol}")

        def compare_levels():
            outputs = [chains[ol].result().value for ol in levels]
            if len(outputs) == 2 and None not in outputs:
                if outputs[0] != outputs[1]:
                    self.fail("goose_in_goose self-compile differs between O0 and O2")
                else:
                    self.ok("goose_in_goose self-compile matches between O0 and O2")

        def show():
            if not cc:
                self.show_later(self.say, "skip native goose_in_goose bootstrap "
                                          "(no C compiler found or --nocgen)")
            for ol in levels:
                self.show(chains[ol])
                if jit:
                    self.show(jits[ol])
                    self.show_later(compare_jit, ol)
            self.show_later(compare_levels)
        return show


def default_jobs():
    """One job per logical processor, up to 32: the C compiler stops getting
    faster well before that, and the rest of the work is mostly short
    processes."""
    return min(os.cpu_count() or 1, 32)


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
    ap.add_argument("--goose-in-goose-only", action="store_true",
                    help="run only the multi-stage Goose-written compiler regression")
    ap.add_argument("-j", "--jobs", type=int, default=default_jobs(),
                    help=f"how many jobs run at once (default {default_jobs()}); "
                         "1 runs everything in order on one thread")
    ap.add_argument("--gpu-jobs", type=int, default=0,
                    help="how many gfx programs run at once (default: no limit "
                         "beyond --jobs)")
    ap.add_argument("--batch", type=int, default=0,
                    help="how many files one `goose --multi-test` run checks, and how "
                         "many programs one executable holds (default: by the number of "
                         "files and jobs); 1 gives each a process and an executable of "
                         "its own")
    args = ap.parse_args()

    if args.nocgen and (args.cc or args.require_clang or args.profile != "baseline"):
        ap.error("--nocgen cannot be combined with a required toolchain or sanitizer profile")
    if args.profile == "sanitize" and (not sys.platform.startswith("linux") or
                                        args.cc not in (None, "clang")):
        ap.error("the sanitize profile requires Linux and Clang")
    if args.jobs < 1 or args.gpu_jobs < 0 or args.batch < 0:
        ap.error("--jobs must be at least 1, and --gpu-jobs and --batch at least 0")

    tc.setup_console()
    started = time.perf_counter()
    # The suite opens no windows: a gfx program asking for one draws off
    # screen instead.
    os.environ["GOOSE_GFX_HEADLESS"] = "1"
    exe = tc.find_goose(args.exe)
    # Everything that changes this process's environment (vcvars) or writes
    # a shared file (the JIT probe) happens here, before any job starts.
    cc = None if args.nocgen else tc.test_cc("clang" if args.profile == "sanitize" else args.cc)
    extra = tc.SANITIZER_FLAGS if args.profile == "sanitize" else ()
    if args.profile == "sanitize":
        tc.use_sanitizer_suppressions()
    # Not under the sanitizers: the program runs inside the compiler process and
    # is not itself instrumented, and its runtime allocations are still held
    # when the compiler exits, which LeakSanitizer reports against the compiler.
    jit = not args.no_jit and args.profile != "sanitize" and tc.have_jit(exe)
    r = Runner(exe, args.jobs)
    r.gpulock = threading.BoundedSemaphore(args.gpu_jobs) if args.gpu_jobs else None
    r.batch = args.batch

    # The bootstrap is the longest chain of work in the suite, so it starts
    # first, and the samples' runner, a process of its own, next.
    show_goose_in_goose = r.goose_in_goose(cc, args.profile, extra, jit)
    if args.goose_in_goose_only:
        show_goose_in_goose()
        r.flush()
        print(f"{r.failures} FAILURE(S)" if r.failures else "all tests passed")
        return int(r.failures != 0)

    # The samples: compiled, built, run and compared with their expected output
    # (or only typechecked without a C compiler), by their own runner, with
    # as many jobs as this one.
    sargs = [sys.executable, str(tc.REPO_ROOT / "samples" / "run_samples.py"),
             "--exe", str(exe), "--jobs", str(args.jobs)]
    if args.gpu_jobs:
        sargs += ["--gpu-jobs", str(args.gpu_jobs)]
    if not jit:
        sargs.append("--no-jit")
    if args.nocgen:
        sargs.append("--nocgen")
    else:
        sargs += ["--profile", args.profile]
        if args.cc:
            sargs += ["--cc", args.cc]

    def samples():
        result = subprocess.run(sargs, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        r.say(tc.decode(result.stdout).rstrip("\n"))
        if result.returncode != 0:
            r.local.out.failures += 1
    # On a thread of its own rather than one of the pool's, which it would
    # keep waiting the whole time.
    samples_future = r.task(samples, alone=True)

    clang = None if args.nocgen or args.profile == "sanitize" else tc.find_clang_c()
    if args.require_clang and not clang:
        ap.error("requested secondary C front end is unavailable: clang")
    # What a gfx, physics or ui test program links, by the category directory
    # it is in and what it imports, empty for a compiler built without that
    # layer: those tests then only generate C.
    native = {"audio": tc.audio_link(exe, cc) if cc else [],
              "gfx": tc.gfx_link(exe, cc) if cc else [],
              "physics": tc.physics_link(exe, cc) if cc else [],
              "ui": tc.ui_link(exe, cc) if cc else []}
    print(f"profile: {args.profile}; C backend: {cc.desc if cc else 'none'}; "
          f"JIT backend: {'TinyCC' if jit else 'none'}; " +
          "; ".join(f"{m}: {'linked' if libs else 'not built in'}" for m, libs in native.items()))
    builddir = tc.REPO_ROOT / "build"
    builddir.mkdir(parents=True, exist_ok=True)
    dumpdir = builddir / "dump"
    dumpdir.mkdir(parents=True, exist_ok=True)
    gendir = builddir / "gen" / args.profile
    gendir.mkdir(parents=True, exist_ok=True)
    r.cc, r.native, r.profile, r.extra, r.jit = cc, native, args.profile, extra, jit
    r.levels = ("0", "2") if args.profile == "baseline" else ("2",)
    r.gendir, r.dumpdir = gendir, dumpdir
    deepdir = gendir

    # One level of category directories; nested syntax helpers and the modules
    # of the goose_in_goose bootstrap are exercised through their entry points.
    tests = [f for f in sorted(HERE.glob("*/*.goose"))
             if f.parent.name not in ("errors", "errors_tc", "goose_in_goose") and f.name != "lexer_tokens.goose"]
    if len({f.stem for f in tests}) != len(tests):
        ap.error("fixture names must be unique across categories (shared expected/ and build outputs)")
    # A gfx, physics or ui fixture with error markers is a rejection test,
    # kept beside what it rejects (for gfx, shaders).
    native_errors = [f for f in tests if f.parent.name in native and error_markers(f)]
    tests = [f for f in tests if f not in native_errors]

    # The generated programs below take long to check; they start before the
    # fixtures. Their files are written here, before any job reads them.
    call_chain_file = deepdir / "call_chain.goose"
    tc.write_text(call_chain_file, call_chain(2000))
    too_deep_file = deepdir / "call_chain_too_deep.goose"
    tc.write_text(too_deep_file, "// error: compile-time call path too deep\n" +
                  call_chain(6000, nest=32))
    guard_runs_file = deepdir / "guard_runs.goose"
    tc.write_text(guard_runs_file, guard_runs(300, 200))

    # The typechecker checks a function inside the call that first reaches it,
    # so its native stack grows with the compile-time call path, which a
    # program can make as long as it likes. A chain of 2000 distinct functions
    # checks and runs. At -O2 the inliner folds its single-use functions into
    # one body per 64 levels of C blocks: one body as deep as the chain would
    # cost time and memory quadratic in its length, and C that MSVC rejects
    # past 128 levels and clang past 256. A chain of 6000 that calls each next
    # function from 32 blocks deep takes more stack than any build of the
    # compiler has, a clang -O3 one holding about 2500 of those calls, and is
    # an error rather than a crash.
    def call_chain_checks():
        f = call_chain_file
        code, out, err = r.goose("-O0", "--check", f)
        if code != 0:
            r.fail(f"typecheck {f.name}", out + err)
        else:
            r.ok(f"typecheck {f.name}")
        if jit:
            code, out, err = r.goose("-O0", "--jit", f)
            if code != 0 or joined(out) != "3":
                r.fail(f"jit {f.name} (exit {code})", out + err)
            else:
                r.ok(f"jit {f.name}")
        if cc:
            cfile = deepdir / "call_chain-O2.c"
            efile = deepdir / f"call_chain-O2{tc.EXE_SUFFIX}"
            code, out, err = r.goose("-O2", "-o", cfile, f)
            if code != 0:
                r.fail(f"cgen -O2 {f.name}", out + err)
            else:
                ok, log = cc.compile(cfile, efile, opt=2 if args.profile == "baseline" else 1,
                                     extra=extra, strict_decls=True, runtime=r.runtime,
                                     log=deepdir / "call_chain-O2.cc.log")
                if not ok:
                    r.fail(f"cc -O2 {f.name}", "\n".join(log.splitlines()[:8]))
                else:
                    code, out, err = tc.run_capture([efile])
                    if code != 0 or joined(out) != "3":
                        r.fail(f"run -O2 {f.name} (exit {code})", out + err)
                    else:
                        r.ok(f"cgen+run -O2 {f.name}")

    def call_chain_too_deep():
        f = too_deep_file
        code, out, err = r.goose("-O0", "--check", f)
        if r.check_error(f, "expected-tc-error", code, out, err):
            r.ok(f"tc-error {f.name}")

    # A guard is an if over the rest of its block (§6.4), whose rest codegen
    # emits after the else rather than in a C block of its own where the else
    # leaves, so 300 guards in a row build with MSVC, which stops at 128
    # levels of blocks, and with clang, at 256. The inliner counts no block
    # for such a rest either: a chain of calls, each behind four guards,
    # folds into bodies MAXNEST (64) levels deep, which blocks opened by
    # codegen but not counted by the inliner would make over 300 deep.
    def guard_runs_level(ol):
        f = guard_runs_file
        cfile = deepdir / f"guard_runs-O{ol}.c"
        efile = deepdir / f"guard_runs-O{ol}{tc.EXE_SUFFIX}"
        code, out, err = r.goose(f"-O{ol}", "-o", cfile, f)
        if code != 0:
            r.fail(f"cgen -O{ol} {f.name}", out + err)
            return
        ok, log = cc.compile(cfile, efile, opt=int(ol) if args.profile == "baseline" else 1,
                             extra=extra, strict_decls=True, runtime=r.runtime,
                             log=deepdir / f"guard_runs-O{ol}.cc.log")
        if not ok:
            r.fail(f"cc -O{ol} {f.name}", "\n".join(log.splitlines()[:8]))
            return
        code, out, err = tc.run_capture([efile])
        if code != 0 or joined(out) != "299 -1 5 3":
            r.fail(f"run -O{ol} {f.name} (exit {code})", out + err)
        else:
            r.ok(f"cgen+run -O{ol} {f.name}")

    deep = [r.task(call_chain_checks), r.task(call_chain_too_deep)]
    deep += [r.task(guard_runs_level, ol) for ol in (r.levels if cc else ())]

    # The fixtures' parse, roundtrip and typecheck runs, in batches by their
    # flags, start before the rest of each fixture, which waits for them.
    fronts = {}
    for f in tests:
        line = first_line(f)
        if "dump-runtime" not in line:
            fronts.setdefault(tuple(front_args(line, has_bce_annotations(f))), []).append(f)
    for args_, files in fronts.items():
        r.fronts.update(r.goose_each(list(args_), files))
    # The fixtures that take longest, those building and running graphics,
    # physics or ui programs, start first.
    order = sorted(tests, key=lambda f: not native_modules(f, native))
    generated = {f: r.submit(r.fixture, f) for f in order}
    # The C of the other fixtures builds in batches, each kind of build
    # (build_options) by itself, and the rest of each fixture follows once
    # its batches are built.
    builds = {}
    plain = [f for f in order if not native_modules(f, native)]
    kinds = {ol: [f for f in plain if "parse-only" not in first_line(f)] for ol in r.levels}
    kinds["dump"] = [f for f in plain if "dump-runtime" in first_line(f)]
    kinds["debug"] = [f for f in plain if debug_fixture(f, first_line(f))]
    for key, together in kinds.items() if cc and r.batch != 1 else ():
        size = r.batch or max(2, min(32, len(together) // (2 * args.jobs)))
        for i in range(0, len(together), size):
            chunk = {f: generated[f] for f in together[i:i + size]}
            future = r.submit(r.build_programs, key, chunk)
            for f in chunk:
                builds.setdefault(f, {})[key] = future
    fixtures = {f: r.submit(r.fixture_runs, f, generated[f], builds.get(f, {})) for f in order}

    # --- what the log shows, in order ---------------------------------------

    def lexer_tokens():
        code, out, err = r.goose("--tokens", HERE / "syntax" / "lexer_tokens.goose")
        if code != 0:
            r.fail("lex lexer_tokens.goose", out + err)
        elif r.check_stdout("lexer_tokens", "lexer_tokens.goose", joined(out)):
            r.ok("lex lexer_tokens.goose")
    r.show_task(lexer_tokens)

    # The shader compiler is built into every compiler, SDL or not: a shader
    # compiles, #include included, and a binding outside SDL_GPU's sets is
    # rejected with the rule it broke.
    def shader_probe():
        code, out, err = r.goose("--compile-shader", HERE / "gfx" / "probe.frag")
        want = "samplers 1, storage textures 0 ro / 0 rw, storage buffers 0 ro / 0 rw, uniform blocks 1 (32 bytes)"
        if code != 0 or want not in out:
            r.fail("compile-shader probe.frag", out + err)
        else:
            r.ok("compile-shader probe.frag")

    def shader_badset():
        code, out, err = r.goose("--compile-shader", HERE / "gfx" / "probe_badset.frag")
        if code != 1 or "sampler 'tex' is in set 0, and must be in set 2" not in err:
            r.fail("compile-shader probe_badset.frag", out + err)
        else:
            r.ok("compile-shader probe_badset.frag")
    r.show_task(shader_probe)
    r.show_task(shader_badset)

    # An error in shader source written in the program is reported at its own
    # line of the program: the one using `oops` in these fixtures.
    def shader_error_line(name):
        f = HERE / "gfx" / f"{name}.goose"
        lines = f.read_text(encoding="utf-8").splitlines()
        at = next(i for i, text in enumerate(lines, 1) if "oops" in text and not text.startswith("//"))
        code, out, err = r.goose("--check", f)
        if code != 1 or f"{f.name}:{at}: error: embed_shader: undeclared identifier 'oops'" not in err:
            r.fail(f"shader error line {f.name}", out + err)
        else:
            r.ok(f"shader error line {f.name}")
    for name in ("gfx_err_shader_syntax", "gfx_err_shader_part"):
        r.show_task(shader_error_line, name)

    # Both sides of each native module's C boundary describe it: they must
    # agree.
    def api(module):
        problems = api_check.check(module)
        what = f"{module}-api stdlib/{module}.goose against its C layer's header"
        if problems:
            r.fail(what, "\n".join(problems))
        else:
            r.ok(what)
    for module in native:
        r.show_task(api, module)

    for f in tests:
        r.show(lambda f=f: fixtures[f].result().front)

    # The optimizer runs at -O1 in every typecheck above; also exercise the
    # other levels (and the --specs dump path) on the optimizer coverage file.
    def optimize(lvl):
        code, out, err = r.goose(lvl, "--check", "--specs", HERE / "optimizer" / "optimize.goose")
        if code != 0:
            r.fail(f"optimize {lvl}", out + err)
        elif r.check_optimizer(lvl, out):
            r.ok(f"optimize {lvl}")
    for lvl in ("-O0", "-O1", "-O2"):
        r.show_task(optimize, lvl)

    # Every annotated regression, including the expected-abort cases. These
    # describe the default O1 pass; O0/O2 execution checks semantics.
    for f in tests:
        r.show(lambda f=f: fixtures[f].result().bce)

    # --- codegen: generate C, compile, run, compare ------------------------
    if not cc:
        r.show_later(r.say, "skip codegen run tests (no C compiler found or --nocgen)")
    else:
        for section in ("cgen", "dump", "debug"):
            for f in tests:
                r.show(lambda f=f, section=section: getattr(fixtures[f].result(), section))

        # Algebraic properties of the compiler's root domain are easier to
        # exhaust over small abstract states than to express in Goose.
        def compiler_roots():
            name = "compiler_roots"
            out_exe = gendir / f"{name}{tc.EXE_SUFFIX}"
            ok, log = cc.compile(HERE / f"{name}.cpp", out_exe, cpp=True,
                                 opt=2 if args.profile == "baseline" else 1,
                                 extra=extra, log=gendir / f"{name}.cc.log")
            if not ok:
                r.fail(f"compiler-cc {name}", log)
            else:
                code, out, err = tc.run_capture([out_exe])
                if code != 0 or tc.sanitizer_failure(err):
                    r.fail(f"compiler {name}", out + err)
                else:
                    r.ok(f"compiler {name}")
        r.show_task(compiler_roots)

        # Direct runtime lifecycle checks use small region limits and allocator
        # instrumentation that cannot be expressed by a Goose program. Keep
        # this one focused native test in both profiles.
        def runtime_lifecycle():
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
        r.show_task(runtime_lifecycle)

        # The same coverage test through clang, release and debug. A compiler
        # that accepts more C than the standard does is not what checks the
        # generated C is actually valid: a call to a function defined only in
        # debug builds compiled silently under MSVC and broke every clang
        # release build.
        def clang_codegen():
            src = gendir / "cgclang.c"
            code, out, err = r.goose("-O2", "-o", src, HERE / "codegen" / "codegen_exec.goose")
            if code != 0:
                r.fail("cgen-clang codegen_exec.goose", out + err)
                return
            for label in ("release", "debug"):
                out_exe = gendir / f"cgclang-{label}{tc.EXE_SUFFIX}"
                ok, log = clang.compile(src, out_exe, opt=1, warn="off", strict_decls=True,
                                        defines=["GS_DEBUG=1"] if label == "debug" else [],
                                        runtime=r.runtime, log=gendir / f"cgclang-{label}.log")
                if not ok:
                    r.fail(f"cgen-clang-{label} codegen_exec.goose",
                           "\n".join(log.splitlines()[:8]))
                    continue
                out = r.run_expected([out_exe], "codegen_exec", f"clang-{label} codegen_exec.goose")
                if out is not None and r.check_stdout("codegen_exec", f"clang-{label}", out):
                    r.ok(f"cgen-clang-{label} codegen_exec.goose")
        if args.profile == "sanitize":
            pass  # The full generated-C suite already ran through Clang.
        elif not clang:
            r.show_later(r.say, "skip cgen-clang (no clang found)")
        else:
            r.show_task(clang_codegen)

    show_goose_in_goose()

    # --- JIT: the same programs, compiled and run inside the compiler --------
    # No C file, no external toolchain: what this checks is that the generated
    # C is portable enough for a third, very different C implementation, and
    # that a program means the same when TinyCC builds it.
    if not jit:
        r.show_later(r.say, "skip JIT run tests (sanitizer profile, --no-jit, or a compiler "
                            "built without the TinyCC backend)")
    else:
        for section in ("jit", "jitdebug"):
            for f in tests:
                r.show(lambda f=f, section=section: getattr(fixtures[f].result(), section))

        def jit_skips():
            skipped = [name for f in tests for name in fixtures[f].result().jitskips]
            if skipped:
                r.say(f"skip {len(skipped)} JIT test(s) the backend cannot run yet: "
                      + ", ".join(sorted(set(skipped))))
        r.show_later(jit_skips)

    parse_errors = sorted((HERE / "errors").glob("*.goose"))
    parse_runs = r.goose_each(["--parse"], parse_errors)

    def parse_error(f):
        code, out, err = r.screened(f, parse_runs[f]())
        if r.check_error(f, "expected-error", code, out, err):
            r.ok(f"error {f.name}")
    for f in parse_errors:
        r.show_later(parse_error, f)

    def native_skips():
        skipped = [name for f in tests for name in fixtures[f].result().nativeskips]
        if skipped:
            r.say(f"skip running {len(set(skipped))} audio, gfx, physics or ui test(s) (no "
                  f"layer for them, or no GPU device): " + ", ".join(sorted(set(skipped))))
    r.show_later(native_skips)

    # Typecheck error tests: must parse, must fail the typechecker. One
    # compiler run tells the two apart by whether it reported the parse done.
    tc_errors = sorted((HERE / "errors_tc").glob("*.goose")) + native_errors
    tc_runs = r.goose_each(["--check"], tc_errors)

    def tc_error(f):
        code, out, err = r.screened(f, tc_runs[f]())
        if not re.search(r"^parsed ok:", out + err, re.M):
            r.fail(f"tc-error-parses {f.name}", out + err)
        elif r.check_error(f, "expected-tc-error", code, out, err):
            r.ok(f"tc-error {f.name}")
    for f in tc_errors:
        r.show_later(tc_error, f)

    for future in deep:
        r.show(future)

    r.show(samples_future)

    r.flush()
    if r.pool:
        r.pool.shutdown()
    print(f"({time.perf_counter() - started:.0f}s with {args.jobs} job(s))")
    if r.failures:
        print(f"{r.failures} FAILURE(S)")
        return 1
    print("all tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
