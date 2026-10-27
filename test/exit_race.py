"""Threads ending the process at once: the main program and eight workers each
write to stderr and call exit(7) together, with the output redirected, which
crashed in the C library's teardown on Windows when two exits ran it at once.
Every run must end with the code they all exit with."""
import subprocess
import tempfile
from pathlib import Path

import toolchain as tc

PROGRAM = """\
import os;

struct Go { n: i64 }

thread_fn worker() {
    qget<Go>();
    write_stderr("worker exiting\\n");
    exit(7);
}

fn main() {
    for i in 8 { thread_spawn(worker); }
    for i in 8 { qput(Go { n: i }); }
    write_stderr("main exiting\\n");
    exit(7);
}
"""

RUNS = 20


def check_exit_race(exe, base):
    """Raises RuntimeError on a failure."""
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="exit-race-", dir=base) as tmp:
        root = Path(tmp)
        source = root / "exit_race.goose"
        tc.write_text(source, PROGRAM)
        app = root / ("exit_race" + tc.EXE_SUFFIX)
        r = subprocess.run([str(exe), "build", "-o", str(app), str(source)], cwd=root,
                           capture_output=True, text=True, errors="replace", timeout=300)
        if r.returncode != 0:
            raise RuntimeError(f"exit race: goose build:\n{r.stdout}{r.stderr}")
        for i in range(RUNS):
            with open(root / "out.txt", "wb") as out, open(root / "err.txt", "wb") as err:
                code = subprocess.run([str(app)], cwd=root, stdout=out, stderr=err,
                                      timeout=60).returncode
            text = tc.decode((root / "err.txt").read_bytes())
            if code != 7 or "exiting" not in text:
                raise RuntimeError(f"exit race: run {i + 1} of {RUNS} ended with {code}, "
                                   f"not 7:\n{text}")
