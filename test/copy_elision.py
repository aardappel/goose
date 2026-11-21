"""Copy elision against its reference: programs made from a seed, run with the
copies a fixed struct over 256 bytes takes left out where the code generator
finds nothing can tell (the default) and with every copy made
(--no-copy-elision), at -O0 and -O2, must all print the same.

The programs mix what the elision's conditions guard: a variable both an
argument and the call's destination, written by a later argument, a function
value or a reference while the callee reads it, handed on to a `var`
parameter directly, through dispatch or through a parameter of the caller's
own, at its only use or in a loop, to callees kept as calls (`recursive`) and
callees the inliner takes.

Run on its own for a wider sweep:

    python test/copy_elision.py --exe build/goose --seeds 0-499 [--cc]

which prints the seeds whose programs disagree and keeps their sources."""
import argparse
import random
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "scripts"))
import toolchain as tc  # noqa: E402

# The seeds the test suite runs: each program holds CASES cases.
SUITE_SEEDS = range(8)
CASES = 24

PRELUDE = """\
import std;

struct Big { a: i64 = 0, b: i64 = 0, c: i64 = 0, rest: i64[40] = [0; 40] }
enum Shape {
    Thin { n: i64 },
    Wide { w: i64 },
}
var g = Big { a: 1, b: 2, c: 3, .. };
var shapes: Shape[>..] = [];

fn sum(d: Big) -> i64 { d.a + d.b * 3 + d.c * 7 }
"""

# The callees, each twice: `recursive` keeps one a call at -O2, while the
# inliner may take the other. {R} is the prefix, {S} the suffix of the name.
CALLEES = """\
{R}fn peek{S}(d: Big, k: i64) -> i64 {{ d.a * 100 + d.b * 10 + k }}
{R}fn poke{S}(var d: Big, k: i64) -> i64 {{ d.a = k; d.a + d.b }}
{R}fn mirror{S}(d: Big) -> Big {{ Big {{ a: d.b, b: d.a, c: d.c + 1, .. }} }}
{R}fn bump{S}(var d: Big) -> Big {{ d.a += 1; d }}
{R}fn swap2{S}(x: Big, y: Big) -> Big {{ Big {{ a: y.b, b: x.a, c: x.c + y.c, .. }} }}
{R}fn both{S}(x: Big, var y: Big) -> i64 {{ y.a = 40; x.a * 10 + y.a }}
{R}fn rebuild{S}(var d: Big, k: i64) -> Big {{
    let o = Big {{ a: d.b, b: d.a, c: k, .. }};
    if k > 1 {{ return o; }}
    d.c = k;
    d
}}
{R}fn setg{S}(d: Big) -> i64 {{ g.a += 5; d.a + g.a }}
{R}fn fromg{S}(var d: Big) -> Big {{ d.b = g.a; d }}
{R}fn relay{S}(d: Big) -> Big {{ bump{S}(d) }}
{R}fn relay_ro{S}(d: Big) -> i64 {{ peek{S}(d, 1) }}
{R}fn ro_ref{S}(d: Big, r: Big&) -> i64 {{ r.a = 50; d.a }}
{R}fn tag{S}(s: Shape.Thin, d: Big) -> i64 {{ d.a + s.n }}
{R}fn tag{S}(s: Shape.Wide, var d: Big) -> i64 {{ d.a = 60; d.a + s.w }}
{R}fn relay_tag{S}(s: Shape, d: Big) -> i64 {{ tag{S}(s, d) }}
fn with_f{S}<F>(d: Big) -> i64 {{ let k = F(1); d.a * 100 + k }}
"""

SUFFIXES = (("recursive ", "_r"), ("", "_i"))

# Callees giving a Big: name, then the kinds of their arguments after it
# (B a Big, I an i64).
BIG_FNS = [("mirror", "B"), ("bump", "B"), ("swap2", "BB"), ("rebuild", "BI"),
           ("fromg", "B"), ("relay", "B")]
# Callees giving an i64.
INT_FNS = [("peek", "BI"), ("poke", "BI"), ("both", "BB"), ("setg", "B"),
           ("relay_ro", "B"), ("sum", "B")]


class Var:
    def __init__(self, name, writable, printed):
        self.name, self.writable, self.printed = name, writable, printed


class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)
        self.n = 0

    def num(self):
        return self.r.randint(0, 9)

    def fresh(self, base):
        self.n += 1
        return f"{base}{self.n}"

    def sfx(self):
        return self.r.choice(SUFFIXES)[1]

    def big(self, vs, depth, used):
        """A Big argument: mostly a variable, else a global, a call or a
        literal. `used` lists the variables named so far in this call, which
        a later argument then picks again more often."""
        roll = self.r.random()
        if vs and roll < 0.6:
            v = self.r.choice(used) if used and self.r.random() < 0.5 else self.r.choice(vs)
            used.append(v)
            return v.name
        if roll < 0.7:
            return "g"
        if roll < 0.85 and depth < 2:
            return self.big_call(vs, depth + 1, used)
        return f"Big {{ a: {self.num()}, b: {self.num()}, .. }}"

    def int_arg(self, vs, used):
        """An i64 argument, which may write a variable an earlier argument
        named: a later argument that runs statements."""
        w = [v for v in used if v.writable] or [v for v in vs if v.writable]
        roll = self.r.random()
        if w and roll < 0.4:
            v = self.r.choice(w)
            return f"block {{ {v.name}.a = {self.num()}; {self.num()} }}"
        if vs and roll < 0.6:
            return f"{self.r.choice(vs).name}.b"
        return str(self.num())

    def args(self, kinds, vs, depth, used):
        out = []
        for k in kinds:
            out.append(self.big(vs, depth, used) if k == "B" else self.int_arg(vs, used))
        return ", ".join(out)

    def big_call(self, vs, depth, used):
        name, kinds = self.r.choice(BIG_FNS)
        return f"{name}{self.sfx()}({self.args(kinds, vs, depth, used)})"

    def int_call(self, vs, used):
        name, kinds = self.r.choice(INT_FNS)
        s = "" if name == "sum" else self.sfx()
        return f"{name}{s}({self.args(kinds, vs, 0, used)})"

    def stmt(self, vs, ind, depth=0):
        r = self.r
        w = [v for v in vs if v.writable]
        roll = r.random()
        if w and roll < 0.25:
            return [f"{ind}{r.choice(w).name} = {self.big_call(vs, 0, [])};"]
        if roll < 0.40:
            t = self.fresh("t")
            return [f"{ind}let {t} = {self.big_call(vs, 0, [])};", f"{ind}acc += sum({t});"]
        if roll < 0.55:
            return [f"{ind}acc += {self.int_call(vs, [])};"]
        if w and roll < 0.62:
            return [f"{ind}{r.choice(w).name}.b += {self.num()};"]
        if vs and roll < 0.70:
            v = r.choice(vs).name
            return [f"{ind}for s in shapes {{ acc += {r.choice(['tag', 'relay_tag'])}"
                    f"{self.sfx()}(s, {v}); }}"]
        if w and roll < 0.75:
            v = r.choice(w).name
            return [f"{ind}acc += with_f{self.sfx()}({v}) {{ {v}.a = 77; it }};"]
        if w and roll < 0.80:
            v = r.choice(w).name
            return [f"{ind}acc += ro_ref{self.sfx()}({v}, {v});"]
        if roll < 0.90 and depth == 0:
            # A loop, which may declare a variable of its own used once.
            body = []
            inner = list(vs)
            if r.random() < 0.5:
                lv = Var(self.fresh("w"), r.random() < 0.5, False)
                kw = "var" if lv.writable else "let"
                body.append(f"{ind}    {kw} {lv.name} = Big {{ a: i, b: {self.num()}, .. }};")
                inner.append(lv)
            for _ in range(r.randint(1, 2)):
                body += self.stmt(inner, ind + "    ", depth + 1)
            return [f"{ind}for i in 2 {{"] + body + [f"{ind}}}"]
        return [f"{ind}acc += {self.int_call(vs, [])};"]

    def case(self, k):
        """A case function: Big parameters and locals, statements over them,
        and a line printing what it computed and every variable it may
        print. A variable left unprinted may have its one use in a call."""
        r = self.r
        params = []
        for _ in range(r.randint(0, 2)):
            params.append(Var(self.fresh("p"), r.random() < 0.4, r.random() < 0.5))
        local = []
        for _ in range(r.randint(1, 3)):
            local.append(Var(self.fresh("v"), r.random() < 0.7, r.random() < 0.5))
        vs = params + local
        sig = ", ".join(("var " if p.writable else "") + f"{p.name}: Big" for p in params)
        rec = "recursive " if r.random() < 0.5 else ""
        lines = [f"{rec}fn case{k}({sig}) -> i64 {{", "    var acc = 0;"]
        for v in local:
            kw = "var" if v.writable else "let"
            lines.append(f"    {kw} {v.name} = Big {{ a: {self.num()}, b: {self.num()}, "
                         f"c: {self.num()}, .. }};")
        for _ in range(r.randint(2, 5)):
            lines += self.stmt(vs, "    ")
        shown = [v for v in vs if v.printed]
        fields = "".join(f', " ", {v.name}.a, " ", {v.name}.b, " ", {v.name}.c' for v in shown)
        # Not g, which main prints: named once here, it may be at its only use.
        lines.append(f'    print("case{k} ", acc{fields});')
        lines.append("    acc")
        lines.append("}")
        return lines, len(params)

    def program(self):
        out = [PRELUDE]
        for prefix, s in SUFFIXES:
            out.append(CALLEES.format(R=prefix, S=s))
        main = ["fn main() {",
                "    shapes.push(Shape.Thin { n: 2 });",
                "    shapes.push(Shape.Wide { w: 3 });",
                "    var m1 = Big { a: 4, b: 5, c: 6, .. };",
                "    let m2 = Big { a: 7, b: 8, c: 9, .. };",
                "    var total = 0;"]
        for k in range(CASES):
            lines, np = self.case(k)
            out.append("\n".join(lines) + "\n")
            argv = ", ".join(self.r.choice(["m1", "m2", "g"]) for _ in range(np))
            main.append(f"    total += case{k}({argv});")
            main.append('    print("main ", total, " ", m1.a, " ", m1.b, " ", m2.a, " ", '
                        'g.a, " ", g.b);')
        main.append("}")
        out.append("\n".join(main) + "\n")
        return "\n".join(out)


MODES = [("-O0",), ("-O2",), ("-O0", "--no-copy-elision"), ("-O2", "--no-copy-elision")]


def run(argv, cwd):
    return subprocess.run([str(a) for a in argv], cwd=cwd, capture_output=True, text=True,
                          errors="replace", timeout=600)


def outputs(exe, cc, source, root):
    """What the program prints in each mode, or the failure of one."""
    got = {}
    for mode in MODES:
        tag = "".join(mode).replace("-", "")
        if cc:
            app = root / f"{source.stem}_{tag}{tc.EXE_SUFFIX}"
            r = run([exe, "build", *mode, "-o", app, source], root)
            if r.returncode == 0:
                r = run([app], root)
        else:
            r = run([exe, "run", *mode, source], root)
        if r.returncode != 0:
            return None, f"{' '.join(mode)}: exit {r.returncode}\n{r.stdout}{r.stderr}"
        got[mode] = r.stdout
    return got, None


def check_seed(exe, cc, seed, root):
    """None when every mode prints the same, else what differs."""
    source = root / f"elide_{seed}.goose"
    tc.write_text(source, Gen(seed).program())
    got, err = outputs(exe, cc, source, root)
    if err:
        return f"seed {seed}: {err}"
    ref = got[MODES[2]]
    for mode, out in got.items():
        if out != ref:
            a, b = ref.splitlines(), out.splitlines()
            line = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]),
                        min(len(a), len(b)))
            return (f"seed {seed}: {' '.join(mode)} prints otherwise than "
                    f"{' '.join(MODES[2])} from line {line + 1}:\n"
                    f"  {b[line] if line < len(b) else '(nothing)'}\n"
                    f"  {a[line] if line < len(a) else '(nothing)'} (reference)")
    return None


def check_copy_elision(exe, cc, base, seeds=SUITE_SEEDS, jobs=8):
    """Raises RuntimeError naming the seeds whose modes disagree. With `cc`,
    builds with the compiler's toolchain; else runs in process."""
    exe = Path(exe).resolve()
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="copy-elision-", dir=base)
    root = Path(tmp)
    with ThreadPoolExecutor(jobs) as pool:
        bad = [m for m in pool.map(lambda s: check_seed(exe, cc, s, root), seeds) if m]
    # The sources are kept only to look into a disagreement.
    if not bad:
        shutil.rmtree(root, ignore_errors=True)
    if bad:
        raise RuntimeError(f"{len(bad)} of {len(seeds)} programs (kept in {root}):\n" +
                           "\n".join(bad))
    return len(seeds)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", default=str(HERE.parent / "build" / ("goose" + tc.EXE_SUFFIX)))
    ap.add_argument("--seeds", default="0-7", help="a range, as 0-499, or one seed")
    ap.add_argument("--cc", action="store_true", help="build with a C compiler, not the JIT")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    ap.add_argument("--print", type=int, metavar="SEED", help="print a seed's program")
    args = ap.parse_args()
    if args.print is not None:
        print(Gen(args.print).program(), end="")
        return 0
    lo, _, hi = args.seeds.partition("-")
    seeds = range(int(lo), int(hi or lo) + 1)
    try:
        n = check_copy_elision(args.exe, args.cc, Path(tempfile.gettempdir()), seeds,
                               args.jobs)
        print(f"{n} programs: every mode prints the same")
        return 0
    except RuntimeError as e:
        print(e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
