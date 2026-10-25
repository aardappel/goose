"""The runtime's standard input and files at their edges (stdlib/os.goose),
built by `goose build` and, where the JIT runs, run by `goose run`: stdin
read as the bytes piped in, a carriage return before a line break and a
Ctrl-Z among them, which Windows' text mode would translate and stop at;
read_line taking off a carriage return only before a newline, never one the
builder held before or one at EOF; and on POSIX write_file_atomic keeping the
permissions of the file it replaces."""
import os
from pathlib import Path
import stat
import subprocess
import tempfile

import toolchain as tc

PROGRAM = """\
import std;
import os;

// Standard input as the program reads it: with `all`, read_stdin's bytes;
// with `lines`, read_line's lines, each followed by a `|`, into a builder
// that holds `x` and a carriage return already; with a path, nothing, but
// write_file_atomic of `new` over that file.
fn main() {
    let argv = args();
    var got: u8[>..] = [];
    if argv[1] == "all" {
        read_stdin(got);
    } else if argv[1] == "lines" {
        got.append("x\\r");
        while read_line(got) { got.append("|"); }
    } else {
        print(write_file_atomic(argv[1], "new"));
        return;
    }
    print(got.len);
    for b in got { print(b); }
}
"""


def check_runtime_io(exe, base, *, jit):
    """Raises RuntimeError on a failure."""
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="runtime-io-", dir=base) as tmp:
        root = Path(tmp)
        env = dict(os.environ, GOOSE_HOME=str(root / "home"))
        source = root / "runtime_io.goose"
        tc.write_text(source, PROGRAM)
        app = root / ("runtime_io" + tc.EXE_SUFFIX)

        def run(argv, stdin=b""):
            r = subprocess.run([str(a) for a in argv], cwd=root, env=env, input=stdin,
                               capture_output=True, timeout=300)
            return r.returncode, tc.decode(r.stdout), tc.decode(r.stderr)

        code, out, err = run([exe, "build", "-o", app, source])
        if code != 0:
            raise RuntimeError(f"runtime io: goose build:\n{out}{err}")
        runners = [("built", [app])] + ([("JIT", [exe, "run", source, "--"])] if jit else [])
        cases = [("all", b"a\r\nb\x1ac", [6, 97, 13, 10, 98, 26, 99]),
                 ("lines", b"\nab\r\n", [6, 120, 13, 124, 97, 98, 124]),
                 ("lines", b"ab\r", [6, 120, 13, 97, 98, 13, 124]),
                 ("lines", b"\r", [4, 120, 13, 13, 124]),
                 ("lines", b"ab", [5, 120, 13, 97, 98, 124]),
                 ("lines", b"", [2, 120, 13]),
                 ("lines", b"ab\r\ncd\r", [9, 120, 13, 97, 98, 124, 99, 100, 13, 124]),
                 ("lines", b"ab\r\r\n", [6, 120, 13, 97, 98, 13, 124])]
        for how, argv in runners:
            for mode, data, want in cases:
                code, out, err = run(argv + [mode], data)
                if code != 0 or out.split() != [str(n) for n in want]:
                    raise RuntimeError(f"runtime io: the {how} program reading {data!r} with "
                                       f"{mode}: wanted {want}, got exit {code}:\n{out}{err}")
        if tc.IS_WINDOWS:
            return
        for mode in (0o600, 0o755):
            target = root / f"kept_{mode:o}.txt"
            target.write_text("old")
            target.chmod(mode)
            for how, argv in runners:
                code, out, err = run(argv + [target])
                have = stat.S_IMODE(target.stat().st_mode)
                if code != 0 or target.read_text() != "new" or have != mode:
                    raise RuntimeError(f"runtime io: the {how} program's write_file_atomic over a "
                                       f"file of mode {mode:o} left mode {have:o}, exit {code}:"
                                       f"\n{out}{err}")
                target.write_text("old")
