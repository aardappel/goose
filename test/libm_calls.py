"""math's libm functions as the C compiler sees them: gcc and clang still know
them as libm's under the runtime's names (gs_sin, ...), so pow(x, 2.0) is a
multiply and, where the C library has sincos (Linux), sin and cos of one
value are one call to it. gcc knows a libm function only by its own name,
and clang by its symbol."""
import re
import sys
import tempfile
from pathlib import Path

import toolchain as tc

# The values come from the command line, so that the C compiler cannot fold
# the calls away.
PROGRAM = """\
import math;
import os;

fn wave(xs: f64[:]) -> f64 {
    var acc = 0.0;
    for i in xs.len { let x = xs[i]; acc += sin(x) * 3.0 + cos(x); }
    return acc;
}

fn wavef(xs: f32[:]) -> f32 {
    var acc: f32 = 0.0;
    for i in xs.len { let x = xs[i]; acc += sin(x) * cos(x); }
    return acc;
}

fn squares(xs: f64[:]) -> f64 {
    var acc = 0.0;
    for i in xs.len { acc += pow(xs[i], 2.0); }
    return acc;
}

fn main() {
    var xs: f64[>..] = [];
    var fs: f32[>..] = [];
    let half: f32 = 0.5;
    for i in args().len {
        xs.push(i * 0.5);
        fs.push(i * half);
    }
    print(wave(xs), " ", wavef(fs), " ", squares(xs));
}
"""

CALL = re.compile(r"^\s*(?:callq?|jmpq?|bl|b)\s+_*(\w+?)(?:@PLT)?\s*$", re.M)


def check_libm_calls(exe, ccs, base):
    """Checks the C of one program with each gcc-style compiler of `ccs`.
    Returns why it was skipped, or None when the check ran; raises
    RuntimeError on a failure."""
    ccs = [c for c in ccs if c is not None and c.style == "gcc"]
    ccs = [c for i, c in enumerate(ccs) if c.cc not in (d.cc for d in ccs[:i])]
    if not ccs:
        return "no gcc-style C compiler"
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="libm-calls-", dir=base) as tmp:
        root = Path(tmp)
        source = root / "libm.goose"
        tc.write_text(source, PROGRAM)
        cfile = root / "libm.c"
        code, out, err = tc.run_capture([exe, "-o", cfile, source])
        if code != 0:
            raise RuntimeError(f"libm calls: goose -o failed\n{out}{err}")
        for cc in ccs:
            asm = root / f"libm-{cc.name}.s"
            code, out, err = tc.run_capture([cc.cc, "-O2", "-S", "-o", asm, cfile])
            if code != 0:
                raise RuntimeError(f"libm calls: {cc.desc} -S failed\n{out}{err}")
            calls = set(CALL.findall(asm.read_text(encoding="utf-8", errors="replace")))
            absent = {"pow"}
            if sys.platform.startswith("linux"):
                absent |= {"sin", "cos", "sinf", "cosf"}
                missing = {"sincos", "sincosf"} - calls
                if missing:
                    raise RuntimeError(f"libm calls ({cc.desc}): no call to "
                                       f"{', '.join(sorted(missing))}; calls "
                                       f"{', '.join(sorted(calls))}")
            if absent & calls:
                raise RuntimeError(f"libm calls ({cc.desc}): calls "
                                   f"{', '.join(sorted(absent & calls))}; calls "
                                   f"{', '.join(sorted(calls))}")
    return None
