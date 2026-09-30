// A value fits a slot of its own type made `const` (§9.5): adding `const` is
// implicit everywhere, and a copy of a `const` value is a fresh one. Structs,
// variants, fixed arrays, floats and bools land in `const` locals, vars,
// globals, fields, payloads, parameters and results as integers and enums
// always did, `const P<i64>` written as a type included; what stays
// read-only is pinned by errors_tc/const_struct_*. A holder keeps what its
// references point into: they write through as their own types say.
import std;

struct S { n: i64 }
struct Pair<T> { a: T, b: T }
struct Wrap { const inner: S, outer: const S, pair: const Pair<i64> }
struct Nest { s: S, m: i64 }
struct Cfg { level: i64 = 3, name: const u8[:] = "cfg" }
struct Holder { r: i64&, s: const u8[:] }
enum Shape { Dot { const at: S }, Box { w: i64, h: i64 }, None }
enum Opt<T> { Some { v: T }, Nothing }

const ORIGIN: S = S { n: 3 };
var current: const S = S { n: 4 };
const PAIR: Pair<i64> = Pair { a: 5, b: 6 };
var gx = 10;
const GHOLD: Holder = Holder { r: gx, s: "global" };

fn make(n: i64) -> const S { return S { n: n }; }
fn with_default(s: const S = S { n: 7 }) -> i64 { return s.n; }
fn sum(p: const Pair<i64>) -> i64 { return p.a + p.b; }
fn bump(r: i64&) { r = r + 1; }
fn advance() { current = S { n: current.n + 1 }; }

fn shape_str(sh: Shape) -> u8[>..] {
    return match sh {
        Dot d => str("dot ", d.at.n),
        Box b => str("box ", b.w, "x", b.h),
        None => str("none"),
    };
}

fn main_const_struct_slots() {
    // Locals: from a literal, a copy, a call, a field, a pointee, another
    // const value and a branch; a var of a const type is assigned whole.
    const lit: S = S { n: 1 };
    let s = S { n: 2 };
    const copied: S = s;
    var v: const S = s;
    v = S { n: 20 };
    v = copied;
    const fromcall: S = make(8);
    let nest = Nest { s: S { n: 9 }, m: 0 };
    const fromfield: S = nest.s;
    let r = &s;
    const frompointee: S = r;
    const fromconst: S = lit;
    const branch: S = if s.n > 1 { s } else { lit };
    print(lit.n, " ", copied.n, " ", v.n, " ", fromcall.n, " ", fromfield.n, " ",
          frompointee.n, " ", fromconst.n, " ", branch.n);
    // A copy of a const value is writable, a call's const result included.
    var plain: S = lit;
    plain.n = 30;
    var again: S = make(31);
    again.n += 1;
    print(plain.n, " ", again.n, " ", lit.n);
    // Defaults, and a parameter's default of a const type.
    const d1: Cfg = default<Cfg>();
    const d2: Cfg = Cfg { level: 4 };
    print(d1.level, " ", d1.name, " ", d2.level, " ", with_default(), " ", with_default(s));
    // Fields: a `const` field, a field of a const type assigned whole, and a
    // generic one.
    var w = Wrap { inner: s, outer: lit, pair: Pair { a: 1, b: 2 } };
    w.outer = S { n: 12 };
    w.pair = PAIR;
    print(w.inner.n, " ", w.outer.n, " ", sum(w.pair));
    // Generic types written const, as a variable's and a parameter's type.
    const pr: Pair<i64> = Pair { a: 1, b: 2 };
    let pp = Pair { a: 3, b: 4 };
    var cp: const Pair<i64> = pp;
    print(sum(pr), " ", sum(cp), " ", sum(PAIR));
    cp = pr;
    var o: const Opt<i64> = Opt<i64>.Some { v: 5 };
    match o {
        Some x => print("some ", x.v),
        Nothing => print("nothing"),
    }
    o = Opt<i64>.Nothing {};
    match o {
        Some x => print("some ", x.v),
        Nothing => print("nothing"),
    }
    // Variants: a payload field of a const type, an enum, a variant type.
    const dot: Shape = Shape.Dot { at: s };
    var sh: const Shape = dot;
    print(shape_str(sh));
    sh = Shape.Box { w: 2, h: 3 };
    print(shape_str(sh));
    const bx: Shape.Box = Shape.Box { w: 4, h: 5 };
    let vb = Shape.Box { w: 6, h: 7 };
    var cb: const Shape.Box = vb;
    print(bx.w, "x", bx.h, " ", cb.w, "x", cb.h);
    cb = bx;
    print(cb.w);
    // A fixed array copied, floats and bools.
    let arr = [1, 2, 3];
    var carr: const i64[3] = arr;
    const larr: i64[3] = arr;
    carr = [4, 5, 6];
    const f: f64 = 2.5;
    var g: const f32 = 1.25;
    g = 0.5;
    const half: f64 = g;
    const flag: bool = s.n > 1;
    var small: const i32 = 7;
    small += 1;
    print(carr, " ", larr, " ", f, " ", g, " ", half, " ", flag, " ", small);
    // Globals: static data, a var of a const type, a generic one.
    advance();
    print(ORIGIN.n, " ", current.n, " ", PAIR.a + PAIR.b);
    // A holder keeps its references' roots and their writability (§9.5:
    // `const` is shallow): a const one writes through them, a copy too.
    var x = 5;
    const h: Holder = Holder { r: x, s: "local" };
    bump(h.r);
    let h2 = Holder { r: x, s: "copy" };
    var hv: const Holder = h2;
    bump(hv.r);
    hv = h;
    var hp: Holder = h;
    hp.r = hp.r * 10;
    bump(GHOLD.r);
    print(x, " ", h.r, " ", hv.s, " ", h2.s, " ", GHOLD.r, " ", GHOLD.s);
}

fn main() { main_const_struct_slots(); }
