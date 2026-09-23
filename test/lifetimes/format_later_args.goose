// print, str and format render their arguments in order, each evaluated just
// before its text, and run a type's format overload in the middle of that:
// what the overload shrinks, the arguments after its own may not view. These
// name nothing it shrinks, or render before it runs (§3.7, §5.1, §5.2).
struct T { n: i64 }
struct U { n: i64 }
struct C { n: i64 }
struct V { n: i64 }
struct W { t: T, k: i64 }
var G: i64[>..<] = [1, 2, 3];
var GO: i64[>..] = [1, 2, 3];
var GV: V[>..<] = [V { 1 }, V { 2 }];

// Leaves only its own value in both globals.
fn format(out: u8[>..]&, t: T) {
    G.clear();
    G.push(t.n);
    GO.clear();
    GO.push(t.n);
    format(out, "T", t.n);
}
// Shrinks the grow-shrink global only, so it may run inside a larger
// expression.
fn format(out: u8[>..]&, u: U) {
    G.clear();
    G.push(u.n);
    format(out, "U", u.n);
}
// Clears the builder it is handed.
fn format(out: u8[>..]&, c: C) {
    out.clear();
    format(out, "C", c.n);
}
// Clears the array its value may lie in: the value is copied into the call
// first, and nothing else of it is rendered after.
fn format(out: u8[>..]&, v: V) {
    GV.clear();
    GV.push(V { v.n + 10 });
    format(out, "V", v.n);
}
fn reset() -> i64 {
    G.clear();
    return G.len;
}
// Renders its parameters around T's overload: its caller passes storage the
// overload does not shrink.
fn show(x: W&, s: i64[:]) {
    print(x, " ", T { 12 }, " ", s);
}
// Shrinks its own array under a view named like a later argument of the
// print that first calls it: that print's arguments are not this body's.
fn scratch() -> i64 {
    var a: i64[>..<] = [1, 2];
    let hs = a[..];
    let n = hs.len;
    a.clear();
    return n;
}

fn main() {
    var h: i64[>..] = [20, 21];
    let hs = h[..];
    // After the overload: a view of another array, values, the globals and
    // their elements read afresh, and a view taken afresh.
    print(T { 1 }, " ", hs, " ", h.len);
    print(T { 2 }, " ", G, " ", GO, " ", G[0], " ", GO.len, " ", G[..]);
    // Before it: rendered already when it runs.
    let s = G[..];
    let so = GO[..];
    print(s, " ", so, " ", T { 3 });
    // str and format, and a builder the overload clears.
    let x = str(T { 4 }, " ", hs, " ", G);
    print(x);
    var b: u8[>..] = [];
    b.format(T { 5 }, " ", hs, " ", GO);
    print(b);
    var b2: u8[>..] = [];
    b2.append("xyz");
    b2.format(C { 6 }, " ", hs);
    print(b2);
    // A nested str, and ordinary calls, shrinking before later arguments.
    print(str(U { 7 }), " ", hs, " ", G);
    print(reset(), " ", hs, " ", G);
    print(scratch(), " ", hs);
    // Rendered in place around the overload: storage it does not shrink.
    let w = W { T { 8 }, 9 };
    print(w);
    var ws: W[>..<] = [W { T { 10 }, 11 }];
    print(ws[0]);
    let wr: W& = ws[0];
    print(wr, " ", G, " ", GO);
    let w2 = W { T { 13 }, 14 };
    show(w2, hs);
    // A value the overload takes whole, lying in the array it clears.
    print(GV[1]);
    let vr: V& = GV[0];
    print(vr, " ", GV.len, " ", GV[0].n);
}
