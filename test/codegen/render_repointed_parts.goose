// runtime-debug: print, str and format render a value where it lies (§3.7),
// and an overload rendering one part may re-point the slice holding the rest,
// or rebind a reference on the way to them. The rest are still the parts of
// the value the rendering began with, which the checker keeps alive: an
// array's count and elements are read together, once, and where a struct,
// variant or enum lies is fixed before its first part.
struct E { x: i64 }
var big: E[>..] = [];
var small: E[>..] = [];
var gs: E[:] = big[..];
fn format(out: u8[>..]&, e: E) { gs = small[..]; format(out, e.x); }
fn show(r: E[:]&) { print(r); }

// A slice in a field, and one among slices.
struct F { x: i64 }
struct H { a: i64, s: F[:], b: i64 }
var fbig: F[>..] = [];
var fsmall: F[>..] = [];
var h = H { a: 5, s: fbig[..], b: 6 };
var outer: F[:][>..] = [];
fn format(out: u8[>..]&, f: F) {
    h.s = fsmall[..];
    if outer.len > 0 { outer[0] = fsmall[..]; }
    format(out, f.x);
}

// A resizable array, and an element of one, behind a reference rebound.
struct G { x: i64 }
var gbig: G[>..] = [];
var gsmall: G[>..] = [];
var rg: G[>..]& = gbig;
fn format(out: u8[>..]&, g: G) { rg .= gsmall; format(out, g.x); }
struct B { v: i64 }
struct Q { b: B, c: i64, d: i64 }
var qbig: Q[>..] = [];
var qsmall: Q[>..] = [];
var rq: Q[>..]& = qbig;
fn format(out: u8[>..]&, b: B) { rq .= qsmall; format(out, b.v); }

// Optional references set to null, in a variable and in an array.
struct A { v: i64 }
struct P { a: A, b: i64, c: i64 }
var p1 = P { a: A { v: 1 }, b: 2, c: 3 };
var o: P? = p1;
var refs: P?[>..] = [];
fn format(out: u8[>..]&, a: A) {
    o .= null;
    if refs.len > 0 { refs[0] .= null; }
    format(out, a.v);
}

// Values of other layouts behind a reference rebound: a struct of variable
// size, a variable-mode enum, a struct ending in a resizable array.
struct C1 { v: i64 }
struct V { c: C1, name: u8[], n: i64 }
var v1 = V { c: C1 { v: 1 }, name: "one", n: 11 };
var v2 = V { c: C1 { v: 2 }, name: "twotwo", n: 22 };
var rv: V& = v1;
fn format(out: u8[>..]&, c: C1) { rv .= v2; format(out, c.v); }
struct C2 { v: i64 }
enum T {
    Named { c: C2, name: u8[], n: i64 },
    Plain { x: i64 },
}
var t1: T.. = T.Named { c: C2 { v: 1 }, name: "one", n: 11 };
var t2: T.. = T.Plain { x: 7 };
var rt: T..& = t1;
fn format(out: u8[>..]&, c: C2) { rt .= t2; format(out, c.v); }
struct C3 { v: i64 }
struct R { c: C3, name: u8[], items: i64[>..] }
var r1 = R { c: C3 { v: 1 }, name: "one", items: [] };
var r2 = R { c: C3 { v: 2 }, name: "twotwo", items: [] };
var rr: R& = r1;
fn format(out: u8[>..]&, c: C3) { rr .= r2; format(out, c.v); }

// Elements of variable size, walked with a cursor.
struct K { v: i64 }
struct N { k: K, name: u8[] }
var nbig: N[>..] = [];
var nsmall: N[>..] = [];
var ns: N[:] = nbig[..];
fn format(out: u8[>..]&, k: K) { ns = nsmall[..]; format(out, k.v); }

fn main() {
    for i in 1..4 { big.push(E { x: i }); }
    small.push(E { x: 9 });
    gs = big[..];
    print(gs);                                  // [1, 2, 3]
    gs = big[..];
    print(str(gs));                             // [1, 2, 3]
    gs = big[..];
    var buf: u8[>..] = [];
    format(buf, gs, " ", gs);
    print(buf);                                 // [1, 2, 3] [9]
    gs = big[..];
    var lim: u8[..32] = [];
    format(lim, gs);
    print(lim);                                 // [1, 2, 3]
    gs = big[..];
    show(gs);                                   // [1, 2, 3]
    gs = big[..];
    let r = &gs;
    print(r);                                   // [1, 2, 3]

    for i in 1..4 { fbig.push(F { x: i }); }
    fsmall.push(F { x: 9 });
    h.s = fbig[..];
    print(h);                                   // H { 5, [1, 2, 3], 6 }
    outer.push(fbig[..]);
    outer.push(fbig[..]);
    print(outer);                               // [[1, 2, 3], [1, 2, 3]]

    for i in 1..4 { gbig.push(G { x: i }); }
    gsmall.push(G { x: 9 });
    print(rg);                                  // [1, 2, 3]
    print(rg);                                  // [9]
    for i in 0..3 { qbig.push(Q { b: B { v: 3 * i + 1 }, c: 3 * i + 2, d: 3 * i + 3 }); }
    qsmall.push(Q { b: B { v: 0 }, c: 0, d: 0 });
    print(rq[2]);                               // Q { 7, 8, 9 }

    print(o);                                   // P { 1, 2, 3 }
    print(o);                                   // null
    refs.push(p1);
    print(refs);                                // [P { 1, 2, 3 }]
    print(refs);                                // [null]
    refs[0] .= p1;
    let rs = refs[..];
    print(rs);                                  // [P { 1, 2, 3 }]
    print(rs);                                  // [null]

    print(rv);                                  // V { 1, "one", 11 }
    print(rv);                                  // V { 2, "twotwo", 22 }
    print(rt);                                  // Named { 1, "one", 11 }
    print(rt);                                  // Plain { 7 }
    r1.items.push(5);
    r1.items.push(6);
    r2.items.push(9);
    print(rr);                                  // R { 1, "one", [5, 6] }
    print(rr);                                  // R { 2, "twotwo", [9] }

    nbig.push(N { k: K { v: 1 }, name: "one" });
    nbig.push(N { k: K { v: 2 }, name: "two" });
    nbig.push(N { k: K { v: 3 }, name: "three" });
    nsmall.push(N { k: K { v: 9 }, name: "nine" });
    ns = nbig[..];
    print(ns);                                  // [N { 1, "one" }, N { 2, "two" }, N { 3, "three" }]
}
