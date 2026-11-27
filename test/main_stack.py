"""Linux-only: a recursion that needs more than the usual 8 MB main-thread
stack but less than the 64 MB soft limit raised for an executable, run in
process and built.

The array is read after the recursive call, so the call is not in tail
position and stays a call, and the array stays a native local. At -O2 the C
compiler still keeps that frame (about 4 KB). 3000 levels are about 12 MB for
built code; under `goose run`, TinyCC's frames for this function are about 8 KB,
so 3000 levels are about 24 MB.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

import toolchain as tc

DEPTH = 3000
# buf[n & 511] is n, and the base case returns 0, so the result is depth
# when depth is a multiple of 4 (each pair of steps lands on the next even).
WANT = str(DEPTH)

PROGRAM = f"""\
recursive fn rec(n: i64) -> i64 {{
    var buf: i64[512] = [0; 512];
    var i = 0;
    while i < 512 {{ buf[i] = n; i = i + 1; }}
    if n == 0 {{ return buf[0]; }}
    let deeper = rec(n - 1);
    return buf[n & 511] + (deeper & 1);
}}
fn main() {{ print(rec({DEPTH})); }}
"""


def launch_prefix():
    """On Linux, start the child with an 8 MB soft stack limit. -S leaves the
    hard limit alone, so the process can raise the soft limit again."""
    if not sys.platform.startswith("linux"):
        return []
    return ["sh", "-c", "ulimit -S -s 8192 && exec \"$@\"", "sh"]


def run(argv, cwd):
    return subprocess.run(launch_prefix() + [str(a) for a in argv], cwd=cwd,
                          capture_output=True, text=True, errors="replace", timeout=120)


def check_main_stack(exe, cc, base):
    """Returns why it was skipped, or None when the check ran; raises
    RuntimeError on a failure."""
    if not sys.platform.startswith("linux"):
        return "not Linux"
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="main-stack-", dir=base) as tmp:
        root = Path(tmp)
        source = root / "stack.goose"
        tc.write_text(source, PROGRAM)
        cfile = root / "stack.c"
        r = run([exe, "-o", cfile, source], root)
        if r.returncode != 0:
            raise RuntimeError(f"goose -o:\n{r.stdout}{r.stderr}")
        text = cfile.read_text(encoding="utf-8")
        body = text.split("static gs_i64 rec_g(gs_i64 n_g)", 1)
        if len(body) != 2 or "rec_g(" not in body[1].split("\nstatic ", 1)[0]:
            raise RuntimeError("the recursion was turned into a loop, so it no longer "
                               "needs a deep native stack")
        r = run([exe, "run", source], root)
        if r.returncode != 0 or r.stdout.splitlines() != [WANT]:
            raise RuntimeError(f"goose run: exit {r.returncode}, wanted {WANT}:\n"
                               f"{r.stdout}{r.stderr}")
        if cc:
            app = root / ("stack" + tc.EXE_SUFFIX)
            r = run([exe, "build", "-o", app, source], root)
            if r.returncode != 0:
                raise RuntimeError(f"goose build:\n{r.stdout}{r.stderr}")
            r = run([app], root)
            if r.returncode != 0 or r.stdout.splitlines() != [WANT]:
                raise RuntimeError(f"built: exit {r.returncode}, wanted {WANT}:\n"
                                   f"{r.stdout}{r.stderr}")
    return None
