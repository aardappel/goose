// A fixed-size value above CodeGen::FIXED_PTR_ABI_LIMIT goes to a call by
// pointer and comes back through an out-pointer (C backend only). The
// language still copies (§7.2, §7.3): an argument is the callee's own value,
// and a result is written to its destination when the call is done, however
// the argument, the destination and what the callee reads alias. Each case
// here goes wrong if a call shares storage where it may not.
import std;
import vec;

struct Eight { a: i64 = 0, b: i64 = 0, c: i64 = 0, d: i64 = 0, e: i64 = 0, f: i64 = 0, g: i64 = 0, h: i64 = 0 }
struct Big {
    a: i64 = 0, b: i64 = 0, c: i64 = 0,
    p0: Eight = Eight { .. }, p1: Eight = Eight { .. }, p2: Eight = Eight { .. }, p3: Eight = Eight { .. },
    p4: Eight = Eight { .. }, p5: Eight = Eight { .. }, p6: Eight = Eight { .. }, p7: Eight = Eight { .. },
    p8: Eight = Eight { .. }, p9: Eight = Eight { .. }, pa: Eight = Eight { .. }, pb: Eight = Eight { .. },
}

var g = Big { a: 1, b: 2, .. };
var xs: Big[>..] = [];

// The argument is the global the callee reads after changing its parameter.
fn reads_global(var d: Big) -> Big { d.a = 10; d.b = g.a; d }
// The callee changes the global its read-only parameter was a copy of.
fn writes_global(d: Big) -> i64 { g.a = 99; d.a }
// The destination is the argument: the result reads it after writing.
fn swapped(d: Big) -> Big { var r = Big { .. }; r.a = d.b; r.b = d.a; r }
// The same through a local returned on one path, the argument on the other.
fn either(var d: Big, k: i64) -> Big {
    let other = Big { a: d.b, b: d.a, .. };
    if k > 0 { return other; }
    d.c = 7;
    d
}
// The argument and the destination are in an array the call moves.
fn grows(var d: Big) -> Big {
    for k in 64 { xs.push(Big { a: 100 + k, .. }); }
    d.b = xs[0].a;
    d
}
// The destination is read by the call through a reference.
fn through_ref(r: Big&) -> Big { var v = Big { .. }; v.a = 5; v.b = r.a; v }
// A temporary reused by the callee is still the caller's to read after.
fn bump(var d: Big) -> Big { d.a += 1; d }

// A result built in place reads the global that is its destination.
fn from_global() -> Big { Big { a: 5, b: g.a, c: g.b, .. } }
// Setters returning their parameter, chained.
fn start(k: i64) -> Big { Big { a: k, .. } }
fn set_b(var d: Big, v: i64) -> Big { d.b = v; d }
fn set_c(var d: Big, v: i64) -> Big { d.c = v; d }
// A setter whose deferred body reads the parameter after the return.
var seen = 0;
fn set_deferred(var d: Big, v: i64) -> Big {
    defer { seen = d.b; }
    d.b = v;
    d
}
// A setter that returns the parameter of a nested call.
fn set_both(var d: Big, v: i64) -> Big { set_c(set_b(d, v), v + 1) }
// Two results, each built in place.
fn pair(k: i64) -> Big, Big { return Big { a: k, .. }, Big { a: k + 1, b: k, .. }; }
fn swap_pair(x: Big, y: Big) -> Big, Big { return y, x; }
// A long-distance return carrying a value past a setter.
fn deep_set(var d: Big, k: i64) -> Big {
    if k > 2 { return Big { a: 77, .. } from outer; }
    d.b = k;
    d
}
fn outer(k: i64) -> Big {
    let r = set_c(deep_set(start(k), k), 9);
    r
}
// A recursive function passing a value down and back.
recursive fn count_down(var d: Big, n: i64) -> Big {
    if n == 0 { return d; }
    d.a += n;
    count_down(d, n - 1)
}
recursive fn rec_a(var d: Big, n: i64) -> Big { if n == 0 { return d; } d.b += 1; rec_b(d, n - 1) }
recursive fn rec_b(var d: Big, n: i64) -> Big { if n == 0 { return d; } d.c += 2; rec_a(d, n - 1) }

// Case functions dispatched on a variant whose payload is large.
enum Shape {
    Wide { body: Big },
    Thin { n: i64 },
}
fn grab(w: Shape.Wide) -> Big { set_b(w.body, 3) }
fn grab(t: Shape.Thin) -> Big { Big { a: t.n, .. } }

// Read-only parameters the callee may read in the caller's own variable
// (CodeGen::StableArg), and the cases where it may not.
fn inner_ro(d: Big) -> i64 { d.a + d.b }
fn outer_ro(d: Big) -> i64 { inner_ro(d) * 10 + d.c }
// Built in place while it reads the argument.
fn mirror(d: Big) -> Big { Big { a: d.b, b: d.a, c: d.c + 1, .. } }
// The same variable bound to a writable reference in the call.
fn ro_ref(d: Big, r: Big&) -> i64 { r.a = 50; d.a }
// A function value changing the variable while the callee runs.
fn ro_then<F>(d: Big) -> i64 { let k = F(1); d.a * 100 + k }
// Results written to the argument's fields one by one.
fn rotate3(d: Big) -> Eight, Eight, Eight { return d.p2, d.p0, d.p1; }
// A tail recursion rebinds its read-only parameter (optimize_tre.h).
recursive fn walk(d: Big, n: i64) -> i64 {
    if n == 0 { return d.a; }
    return 1 + walk(Big { a: d.a + 1, .. }, n - 1);
}

// Parameters handed on to a `var` parameter at their one use, which the
// callee may then change in place (CodeGen::ConsumedParam).
fn hand(d: Big) -> Big { bump(d) }
fn hand2(d: Big) -> Big { hand(d) }
fn hand_twice(d: Big) -> i64 { let x = hand(d); x.a * 10 + d.a }
// Handed on after earlier uses, which hands over nothing (CodeGen::SingleUses).
fn vlast(var d: Big) -> Big { d.a = d.a + 9; bump(d) }
fn ro_last(d: Big) -> Big { let k = d.a; set_b(d, k) }
// A literal built in place, reading its argument (CodeGen::LeafAny).
fn widen(d: Big) -> Big { Big { a: d.a, b: d.a + d.b, c: hand(d).a, .. } }

// Small enough for Goose's inliner: a chain of them shares one variable
// (CodeGen::OpenIbFixed), but for the cases where it may not.
fn put_a(var d: Big, v: i64) -> Big { d.a = v; d }
fn pick(var d: Big, k: i64) -> Big {
    if k > 0 { return Big { a: k, b: d.a, .. }; }
    d.c = 1;
    d
}
fn pick_deferred(var d: Big, k: i64) -> Big {
    defer { seen = d.a; }
    if k > 0 { return Big { a: k, .. }; }
    d
}

// A long-distance return out of an inlined body whose named result lives
// at the function's out-pointer: the value reads that result as it is
// built (CodeGen::OpenIbFixed declines).
fn swap_out(var d: Big, k: i64) -> Big {
    if k > 2 { return Big { a: d.b, b: d.a, .. } from outer2; }
    d.c = k;
    d
}
fn outer2(k: i64) -> Big { swap_out(start(k).set_b(9), k) }
fn swap_out3(var d: Big, k: i64) -> Big {
    if k > 2 { return mirror(d) from outer3; }
    d.c = k;
    d
}
fn outer3(k: i64) -> Big { swap_out3(start(k).set_b(9), k) }
// A user format overload taking one by value (CodeGen::EmitUserFormat).
fn format(out: u8[>..]&, d: Big) { format(out, "Big(", d.a, ", ", d.b, ")"); }
// Either side of the threshold: 256 bytes by C value, 264 by pointer.
struct S256 { x: i64[32] = [0; 32] }
struct S264 { x: i64[33] = [0; 33] }
fn put256(var s: S256, v: i64) -> S256 { s.x[31] = v; s }
fn put264(var s: S264, v: i64) -> S264 { s.x[32] = v; s }
// Dispatch whose arms take the shared argument one `var`, one not.
fn tag_with(s: Shape.Wide, var d: Big) -> i64 { d.a = 5; d.a + s.body.a }
fn tag_with(s: Shape.Thin, d: Big) -> i64 { d.a + s.n }

// A result holding a relative reference is built at its destination
// (NeedsResultLocation), which may be the argument: the callee gets a copy
// rather than the variable lent. Recursive, so it stays a call at -O2.
struct Cell { v: i64 }
var cells: Cell[>..] = [];
struct RelBig { r: Cell&<u32 in cells>?, a: i64 = 0, b: i64 = 0, p: Big = Big { .. } }
fn cell_v(r: Cell&?) -> i64 { if r { return r.v; } return -1; }
recursive fn swapped_rel(d: RelBig, n: i64) -> RelBig {
    if n > 0 { return swapped_rel(d, n - 1); }
    RelBig { r: d.r, a: d.b, b: d.a, .. }
}
recursive fn swapped_rel2(d: RelBig, n: i64) -> RelBig {
    if n > 0 { return swapped_rel2(d, n - 1); }
    var x = RelBig { r: d.r, .. };
    x.a = d.b;
    x.b = d.a;
    x
}

fn show(name: u8[:], d: Big) { print(name, " ", d.a, " ", d.b, " ", d.c); }

fn main() {
    g = reads_global(g);
    print("reads_global ", g.a, " ", g.b);
    g.a = 1;
    let got = writes_global(g);
    print("writes_global ", got, " ", g.a);
    var s = Big { a: 1, b: 2, .. };
    s = swapped(s);
    print("swapped ", s.a, " ", s.b);
    var e = Big { a: 1, b: 2, .. };
    e = either(e, 1);
    print("either ", e.a, " ", e.b, " ", e.c);
    e = either(e, 0);
    print("either ", e.a, " ", e.b, " ", e.c);
    xs.push(Big { a: 1, .. });
    xs[0] = grows(xs[0]);
    print("grows ", xs[0].a, " ", xs[0].b, " ", xs.len);
    var r = Big { a: 3, .. };
    r = through_ref(r);
    print("through_ref ", r.a, " ", r.b);
    let base = Big { a: 1, .. };
    let one = bump(base);
    let two = bump(bump(base));
    print("bump ", base.a, " ", one.a, " ", two.a);

    g = Big { a: 1, b: 2, .. };
    g = from_global();
    show("from_global", g);
    let chain = start(4).set_b(5).set_c(6);
    show("chain", chain);
    g = start(7).set_b(8);
    show("chain_global", g);
    xs[1] = start(9).set_c(10);
    show("chain_element", xs[1]);
    let x = start(1);
    let x = set_b(x, x.a + 1);
    show("shadow", x);
    var y = start(2);
    y = set_b(y, 3);
    y = set_c(set_b(y, y.b + 1), y.c + 1);
    show("reassigned", y);
    let dd = set_deferred(start(1).set_b(4), 6);
    show("deferred", dd);
    print("seen ", seen);
    show("set_both", set_both(start(3), 30));
    let p, q = pair(5);
    show("pair", p);
    show("pair", q);
    let u, v = swap_pair(p, q);
    show("swap_pair", u);
    show("swap_pair", v);
    show("outer", outer(1));
    show("outer", outer(5));
    show("count_down", count_down(start(0), 4));
    show("rec", rec_a(start(0), 5));
    var shapes: Shape[>..] = [];
    shapes.push(Shape.Wide { body: start(11) });
    shapes.push(Shape.Thin { n: 12 });
    for sh in shapes { show("grab", grab(sh)); }
    var sum = 0;
    for i in 4 { let z = start(i).set_b(i * 2).set_c(i * 3); sum += z.a + z.b + z.c; }
    print("loop ", sum);

    var k = start(3).set_b(4).set_c(5);
    print("outer_ro ", outer_ro(k), " ", inner_ro(k));
    let m = start(1).set_b(2);
    let m = mirror(m);
    show("mirror_shadow", m);
    k = mirror(k);
    show("mirror_self", k);
    print("ro_ref ", ro_ref(k, k), " ", k.a);
    print("ro_then ", ro_then(k) { k.a = 77; it }, " ", k.a);
    var t = Big { .. };
    t.p0.a = 1;
    t.p1.a = 2;
    t.p2.a = 3;
    var w = Eight { .. };
    w, t.p1, t.p0 = rotate3(t);
    print("rotate3 ", w.a, " ", t.p0.a, " ", t.p1.a, " ", t.p2.a);
    let wk = start(1);
    print("walk ", walk(wk, 3), " ", wk.a);

    var hv = start(20);
    show("hand", hand(hv));
    show("hand_after", hv);
    show("hand2", hand2(hv));
    show("hand_tmp", hand2(start(30).set_b(1)));
    print("hand_twice ", hand_twice(hv));
    let last = start(40);
    show("hand_last", hand(last));
    let base2 = start(5);
    for i in 2 { show("loop_bump", bump(base2)); }
    for i in 2 { let fresh = start(i); show("loop_fresh", hand(fresh)); }
    show("widen", widen(start(2).set_b(3)));
    show("vlast", vlast(start(1)));
    var rl = start(4);
    show("ro_last", ro_last(rl));
    show("ro_last_after", rl);
    let ml = start(6);
    let mk2 = ml.a + 1;
    show("local_last", set_c(ml, mk2));
    show("outer2", outer2(5));
    show("outer2_1", outer2(1));
    show("outer3", outer3(5));
    let fb = start(3).set_b(4);
    print("format ", fb, " ", str(fb));
    let s1 = S256 { .. };
    let s2 = put256(put256(s1, 1), 2);
    let s3 = S264 { .. };
    let s4 = put264(put264(s3, 1), 2);
    print("threshold ", s1.x[31], " ", s2.x[31], " ", s3.x[32], " ", s4.x[32]);
    for sh in shapes {
        let q = start(1);
        print("tag_with ", tag_with(sh, q), " ", q.a);
    }
    show("inlined", start(1).put_a(2).put_a(3).set_b(4));
    let ia = start(5).put_a(6).pick(7).put_a(8);
    show("inlined_pick", ia);
    show("inlined_pick0", start(5).put_a(6).pick(0));
    show("pick_deferred", start(9).pick_deferred(3));
    print("seen ", seen);
    show("pick_deferred0", start(9).pick_deferred(0));
    print("seen ", seen);
    cells.push(Cell { v: 5 });
    var rb = RelBig { r: cells[0], a: 1, b: 2, .. };
    rb = swapped_rel(rb, 1);
    print("swapped_rel ", rb.a, " ", rb.b);
    rb = swapped_rel2(rb, 0);
    print("swapped_rel2 ", rb.a, " ", rb.b, " ", cell_v(rb.r));
}
