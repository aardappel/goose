"""Under -g, the C of a Goose function is charged to its Goose lines by
#line directives, and nothing else may be: the runtime's functions and the
ones the code generator adds (gs_init_globals, gs_program_init, main, ...)
must follow a #line back to the C file itself. Otherwise a breakpoint on a
Goose line gets locations in them, and the debugger shows a Goose line in
main.

This check generates the C of every fixture under test/ with -g and reports
each definition of a gs_* function or of main that sits under a Goose
line, and each function the debug map lists that the C does not have (a
copy that merging identical specializations took out)."""
import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "scripts"))
import toolchain as tc  # noqa: E402

LINE = re.compile(r'#line \d+ "([^"]*)"')
# A definition at the start of a line: `static void gs_init_globals(void) {`.
DEF = re.compile(r"^[A-Za-z_][\w \*]*\b(gs_\w+|main)\s*\([^;]*\)\s*\{\s*$")


def misplaced(c):
    """The gs_* functions and main defined under a Goose #line in C."""
    goose, found = False, []
    for line in c.splitlines():
        m = LINE.match(line)
        if m:
            goose = m.group(1).endswith(".goose")
            continue
        d = DEF.match(line)
        if d and goose:
            found.append(d.group(1))
    return found


def check_file(exe, f, root):
    out = root / (f.stem + "-" + str(abs(hash(str(f)))) + ".c")
    r = subprocess.run([str(exe), "-g", "-o", str(out), str(f)], capture_output=True,
                       text=True, timeout=120)
    if r.returncode != 0 or not out.exists():
        return None, None   # A fixture that is meant not to compile.
    c = out.read_text(encoding="utf-8", errors="replace")
    msgs = []
    bad = misplaced(c)
    if bad:
        msgs.append(f"{', '.join(bad)} under a Goose #line")
    dmap = out.with_suffix(".debug.json")
    if dmap.exists():
        fns = [fn["c"] for fn in json.loads(dmap.read_text(encoding="utf-8"))["functions"]]
        gone = [n for n in fns if not re.search(r"\b" + re.escape(n) + r"\s*\(", c)]
        if gone:
            msgs.append(f"debug map lists {', '.join(gone)}, not in the C")
        dmap.unlink()
    out.unlink()
    rel = f.relative_to(HERE)
    return rel, (f"{rel}: {'; '.join(msgs)}" if msgs else None)


def check_line_directives(exe, base, jobs=8):
    """Raises RuntimeError naming the fixtures whose C charges a gs_*
    function or main to a Goose line, or whose debug map lists a function
    the C lacks; returns how many were checked."""
    exe = Path(exe).resolve()
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="line-directives-", dir=base))
    files = sorted(HERE.rglob("*.goose"))
    try:
        with ThreadPoolExecutor(jobs) as pool:
            results = list(pool.map(lambda f: check_file(exe, f, root), files))
    finally:
        shutil.rmtree(root, ignore_errors=True)
    bad = [m for _, m in results if m]
    if bad:
        raise RuntimeError(f"{len(bad)} fixtures:\n" + "\n".join(bad))
    return sum(1 for rel, _ in results if rel)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", default=str(HERE.parent / "build" / ("goose" + tc.EXE_SUFFIX)))
    ap.add_argument("-j", "--jobs", type=int, default=8)
    args = ap.parse_args()
    try:
        n = check_line_directives(args.exe, tempfile.gettempdir(), args.jobs)
    except RuntimeError as e:
        print(e)
        return 1
    print(f"line directives: {n} fixtures, no gs_* function or main under a Goose line, "
          "no debug map entry without its function")
    return 0


if __name__ == "__main__":
    sys.exit(main())
