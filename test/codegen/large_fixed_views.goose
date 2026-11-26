// A fixed-size argument above CodeGen::FIXED_PTR_ABI_LIMIT is passed as a
// pointer, to the caller's own variable where the callee does not write its
// parameter (CodeGen::ParamUnwritten) and nothing else writes the variable
// meanwhile (CodeGen::StableArg). The parameter is a copy all the same
// (§7.2), also where either is written with no assignment or `&` naming it:
// through a slice, an array converted to one, a member that grows, or a
// by-reference `for`. `recursive` keeps a callee a call at -O2.
struct Xs { xs: i64[40] }
struct Ys { ys: i64[..40] }

// The parameter is written.
recursive fn slice(p: Xs, d: i64) -> i64 { if d > 0 { return slice(p, d - 1); } let s = p.xs[..]; s[1] = 7; p.xs[1] }
recursive fn convert(p: Xs, d: i64) -> i64 { if d > 0 { return convert(p, d - 1); } set(p.xs); p.xs[1] }
recursive fn grow(p: Ys, d: i64) -> i64 { if d > 0 { return grow(p, d - 1); } p.ys.push(2); p.ys.len }
recursive fn each(p: Xs, d: i64) -> i64 { if d > 0 { return each(p, d - 1); } for &x in p.xs { x = 7; } p.xs[1] }
recursive fn store(p: Xs, d: i64) -> i64 { if d > 0 { return store(p, d - 1); } p.xs[1] = 7; p.xs[1] }
fn set(s: i64[:]) { s[1] = 7; }

// The caller's variable is written while it is the argument.
recursive fn beside(p: Xs, s: i64[:], d: i64) -> i64 { if d > 0 { return beside(p, s, d - 1); } s[1] = 7; p.xs[1] }
recursive fn beside_ref(p: Xs, x: i64&, d: i64) -> i64 { if d > 0 { return beside_ref(p, x, d - 1); } x = 7; p.xs[0] }

fn main() {
    let a = Xs { xs: [1; 40] };
    print(slice(a, 0), " ", a.xs[1]);
    let b = Xs { xs: [1; 40] };
    print(convert(b, 0), " ", b.xs[1]);
    var c = Ys { ys: [1] };
    print(grow(c, 0), " ", c.ys.len);
    let d = Xs { xs: [1; 40] };
    print(each(d, 0), " ", d.xs[1]);
    let e = Xs { xs: [1; 40] };
    print(beside(e, e.xs[..], 0), " ", e.xs[1]);
    let f = Xs { xs: [1; 40] };
    for &x in f.xs { print(beside_ref(f, x, 0), " ", f.xs[0]); break; }
    let g = Xs { xs: [1; 40] };
    print(slice(g, 2), " ", g.xs[1]);
    let h = Xs { xs: [1; 40] };
    print(store(h, 0), " ", h.xs[1]);
}
