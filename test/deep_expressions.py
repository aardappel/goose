"""Expressions that nest thousands of levels deep: the C written for them nests
no deeper than C compilers take (clang stops at 256 brackets), their values
come out the same run in process and built, and an operand ahead of the deep
part still runs first."""
import subprocess
import tempfile
from pathlib import Path

import toolchain as tc

TERMS = 2000
NESTED_IFS = 60
MAX_BRACKETS = 200


def program():
    # Parameters rather than constants, which the optimizer would fold.
    plus_y = " + y" * TERMS
    return f"""\
import os;

fn left(x: i64, y: i64) -> i64 {{ return x{plus_y}; }}
fn right(x: i64, y: i64) -> i64 {{ return {"y + (" * TERMS}x{")" * TERMS}; }}
fn float(x: f64, y: f64) -> f64 {{ return x{plus_y}; }}
fn less(x: i64, y: i64) -> bool {{ return (x{plus_y}) < (y{" - x" * TERMS}); }}
fn nested(x: i64, y: i64) -> i64 {{
    {"if x > y { " * NESTED_IFS}return x{plus_y};{" }" * NESTED_IFS}
    return 0;
}}

fn main() {{
    let n = args().len;
    print(left(n, n + 2));
    print(right(n, n + 2));
    print(float(n * 1.0, n * 0.5 + 2.0));
    print(less(n, n + 1));
    print(nested(n, n - 2));
}}
"""


def expected(n=1):
    return [str(n + TERMS * (n + 2)), str(n + TERMS * (n + 2)),
            f"{n + TERMS * (n * 0.5 + 2.0):.1f}", "false", str(n + TERMS * (n - 2))]


# Under --debug, the division by zero has to be reported, not the overflow in
# the sum to its right, which C would be free to compute first.
ORDER = f"""\
import os;

fn order(x: i64, z: i64, y: i64) -> i64 {{ return x / z + {"y + (" * TERMS}y{")" * TERMS}; }}

fn main() {{ let n = args().len; print(order(n, n - 1, 9223372036854775807 - n)); }}
"""


def max_brackets(c):
    """The deepest bracket nesting in the generated code, past the runtime."""
    depth = deepest = 0
    for ch in c[c.find("/* ---- code ---- */"):]:
        if ch in "([{":
            depth += 1
            deepest = max(deepest, depth)
        elif ch in ")]}":
            depth -= 1
    return deepest


def run(argv, cwd):
    return subprocess.run([str(a) for a in argv], cwd=cwd, capture_output=True, text=True,
                          errors="replace", timeout=300)


def check_deep_expressions(exe, cc, base):
    """Raises RuntimeError on a failure; builds with the compiler's own
    toolchain when the runner found a C compiler (`cc`)."""
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="deep-expressions-", dir=base) as tmp:
        root = Path(tmp)
        source = root / "deep.goose"
        tc.write_text(source, program())
        want = expected()
        r = run([exe, "-o", root / "deep.c", source], root)
        if r.returncode != 0:
            raise RuntimeError(f"goose -o:\n{r.stdout}{r.stderr}")
        deepest = max_brackets((root / "deep.c").read_text(encoding="utf-8"))
        if deepest > MAX_BRACKETS:
            raise RuntimeError(f"the C nests {deepest} brackets deep, more than {MAX_BRACKETS}")
        r = run([exe, "run", source], root)
        if r.returncode != 0 or r.stdout.split() != want:
            raise RuntimeError(f"goose run: exit {r.returncode}, wanted {want}:\n"
                               f"{r.stdout}{r.stderr}")
        if cc:
            app = root / ("deep" + tc.EXE_SUFFIX)
            r = run([exe, "build", "-o", app, source], root)
            if r.returncode != 0:
                raise RuntimeError(f"goose build:\n{r.stdout}{r.stderr}")
            r = run([app], root)
            if r.returncode != 0 or r.stdout.split() != want:
                raise RuntimeError(f"built: exit {r.returncode}, wanted {want}:\n"
                                   f"{r.stdout}{r.stderr}")
        order = root / "order.goose"
        tc.write_text(order, ORDER)
        r = run([exe, "run", "--debug", order], root)
        if r.returncode == 0 or "division by zero" not in r.stderr:
            raise RuntimeError(f"goose run of a division ahead of a deep sum: exit "
                               f"{r.returncode}:\n{r.stdout}{r.stderr}")
