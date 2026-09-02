// Optimizer coverage: inlining, constant folding, and propagation shapes.
// Not a meaningful program; it exercises every optimizer path and must keep
// typechecking (and meaning the same thing) at every -O level.

// --- inlining fodder ---------------------------------------------------------

fn tiny_add(a: i64, b: i64) -> i64 { a + b }         // Multi-use, below NC.
fn tiny_flt(x: f64) -> f64 { x * 2.0 }
fn once_used(n: i64) -> i64 {                        // Single use, large body.
    var s = 0;
    for i in n { s += i * i; }
    if s > 100 { s = s - 100; }
    while s > 17 { s -= 17; }
    s
}
fn medium(v: i64, w: i64) -> i64 {                   // Lands in NCU territory.
    let a = v * 2 + w;
    let b = a - v / 3;
    if a > b { a } else { b }
}
fn big_not_inlined(n: i64) -> i64 {                  // Over every threshold.
    var s = 1;
    for i in n { s += i; s *= 3; s -= i / 2; s += i % 5; }
    for j in n { s += j * j; s -= j * 2; s += 1; }
    if s < 0 { s = 0 - s; }
    while s > 1000 { s /= 7; s -= 3; }
    s
}
fn returns_early(n: i64) -> i64 {                    // Return statements in loops.
    if n < 0 { return -1; }
    for i in n { if i == 3 { return i * 10; } }
    n
}
fn void_helper(v: i64) {                             // Void result + bare guard.
    guard v > 0;
    print(v);
}
fn var_param(var x: i64) -> i64 { x += 1; x * 2 }    // Written param: no const subst.
fn two_rets(a: i64) -> i64, i64 { return a, a + 1; } // Multi-value: never inlined.
recursive fn fact_op(n: i64) -> i64 { if n <= 1 { 1 } else { n * fact_op(n - 1) } }

fn use_inlining() {
    print(tiny_add(1, 2) + tiny_add(3, 4));          // Constant args cascade to 10.
    print(tiny_flt(2.5));
    print(once_used(9));
    print(medium(1, 2) + medium(3, 4) + medium(5, 6));
    print(big_not_inlined(4));
    print(returns_early(7));
    void_helper(0);
    void_helper(5);
    print(var_param(41));
    let a, b = two_rets(1);
    print(a + b);
    print(fact_op(5));
}

// --- reference/slice/nonfixed parameters through inlining --------------------

fn bump(r: i64&) { r += 1; }                         // Reference binds; writes pointee.
fn doubled(v: i64) -> i64 { v * 2 }
fn total_op(xs: i64[:]) -> i64 {
    var s = 0;
    for x in xs { s += x; }
    s
}
fn takes_string(s: u8[]) -> i64 { s.len }            // Variable-size by value.
fn grow(v: i64[>..]&) { v.push(1); v.push(2); }      // Growing through a reference.
fn make_str(n: i64) -> u8[>..] {                     // Nonfixed return (NRVO shape).
    var s: u8[>..] = "x";
    for i in n { s.append("y"); }
    return s;
}

fn use_params() {
    var x = 10;
    bump(&x);
    x = x.doubled();                                 // UFCS receiver becomes param 0.
    let arr = [1, 2, 3, 4];
    print(total_op(arr) + arr.len);                     // .len of a fixed array folds.
    print(takes_string("hello"));
    var v: i64[>..] = [];
    grow(&v);
    print(v.len);
    print(x);
    let s = make_str(3);
    print(s.len);
}

// --- constant folding and propagation ----------------------------------------

let G = 6;                                           // let global: propagates.
var gv = 100;                                        // Written below: stays a load.

fn folding() {
    let a = 2 + 3 * 4 - 1;                           // 13.
    let b = (a << 2) | 1;                            // a propagates, then folds.
    let c = ~b ^ 0xF0;
    let d = -a + (100 / 7) % 3;
    let e = 2.5 * 4.0 - 1.0 / 8.0;
    let f = 1 < 2 && 3 >= 3 || false;
    let g2 = !(1 == 2) && !false;
    let h = G * 7;                                   // Global constant propagation.
    print(a + b + c + d);
    print(e);
    print(f && g2);
    print(h);
    gv += 1;
    print(gv);
    // A runtime abort survives folding: overflow wraps in release, so this
    // stays an addition rather than becoming a constant.
    print(9223372036854775807 + a);
}

// --- casts_op -------------------------------------------------------------------

struct Bytes { b: u8, w: u16, f: f32 }

fn casts_op() {
    var s = Bytes { 1, 2, 0.5 };
    s.b = 300 as! u8;                                // Wraps: folds to 44.
    s.w = 70000 as! u16;
    s.f = s.f * 2.0 + 0.25;                          // f32 math, not constant.
    let i = 3.0 as i64;                              // Integral: folds.
    let f2 = 7 as f64;
    let g3 = 2.5 as! i64;                            // as! truncation folds.
    let h2 = 1.5 as f32;                             // Exact in f32: folds.
    let big = 10000000000000000000.5 as! i64;        // Out of range: left alone.
    print(s.b + s.w + i + g3 + big);
    print(f2 + (h2 as f64) + (s.f as f64));
}

// --- control-flow folding ----------------------------------------------------

fn controlflow() -> i64 {
    let t = true;
    if t { print(1); } else { print(2); }            // Else branch dropped.
    if 1 > 2 { print(3); }                           // Whole statement dropped.
    let v = if G == 6 { 10 } else { 20 };            // Value if folds to 10.
    while false { print(4); }                        // Dropped.
    var i = 0;
    while i < 3 { i += 1; }                          // Kept.
    guard 1 < 2 else { return -1; }                  // True guard folds away.
    match 5 { 0 => print(0), 1..9 => print(1), _ => print(2) }
    let m = match G { 6 => 60, _ => 0 };             // Constant match value.
    assert(true);                                    // Removed.
    assert(m > 0);                                   // Condition folds, then removed.
    loop {
        guard i < 6;                                 // Bare guard: implicit break.
        i += 1;
    }
    block {
        guard m == 60;                               // Folds true inside a block.
        print(m);
    }
    if false { return 99; }                          // Dropped; flow continues.
    m + v
}

// --- propagation limits ------------------------------------------------------

fn limits() {
    var w = 5;
    w = 6;                                           // Written: not propagated.
    let addr = 7;
    let r = &addr;                                   // Address taken: not propagated.
    print(w + addr + r);
    let cap: i64[..4] = [1, 2];
    print(cap.cap + cap.len);                        // .cap folds; .len stays dynamic.
}

// --- HOFs, function values, captures -----------------------------------------

fn apply<F>(x: i64) -> i64 { F(x) }
fn twice<F>(x: i64) -> i64 { F(F(x)) }
fn bigger_hof<F>(n: i64) -> i64 {
    var s = 0;
    for i in n {
        s += F(i);
        if s > 50 { s -= 25; }
        while s > 40 { s -= 7; }
    }
    s
}

fn hofs_op() {
    let base = 3;                                    // Captured let: decl must stay.
    print(apply(4) { it + base });
    print(twice(2) { x => x * x });
    print(bigger_hof(10) { it * 2 + base });
    var acc = 0;                                     // Captured and written in a block.
    apply(1) { acc += it; it };
    print(acc);
}

fn nested_host(n: i64) -> i64 {
    fn helper(k: i64) -> i64 { k * 3 }               // Nested, no captures.
    var t = 0;
    fn addt(k: i64) { t += k; }                      // Captures t.
    addt(helper(n));
    addt(2);
    t
}

// --- case functions and match binders ----------------------------------------

enum Shape_op {
    Circle { r: f64 },
    Rect { w: f64, h: f64 },
    Point,
}

fn area_op(c: Shape_op.Circle) -> f64 { c.r * c.r * 3.14159 }
fn area_op(r: Shape_op.Rect) -> f64 { r.w * r.h }
fn area_op(p: Shape_op.Point) -> f64 { 0.0 }

fn shapes() {
    var s: Shape_op = Shape_op.Rect { w: 2.0, h: 3.0 };
    print(area_op(s));                                  // Tag dispatch: not inlined.
    let c = Shape_op.Circle { r: 1.0 };
    print(area_op(c));                                  // Direct variant call: inlinable.
    match s {
        Circle cc => print(cc.r),
        Rect rr   => print(rr.w * rr.h),
        Point     => print(0.0),
    }
}

// --- long-distance returns ---------------------------------------------------

fn deep_fail(x: i64) -> i64 {
    guard x >= 0 else { return -1 from outer_op; }
    x * 2
}
fn outer_op(x: i64) -> i64 {
    let v = deep_fail(x);                            // Callee unwinds to outer_op.
    v + 1
}
fn longdistance_op() {
    print(outer_op(5));
    print(outer_op(0 - 5));
}

// --- function-value returns exit the defining function -----------------------

fn each<F>(n: i64) { for i in n { F(i); } }
fn finder(n: i64) -> i64 {
    each(n) { if it == 2 { return it * 100; } };     // Exits finder, through each.
    -1
}

// --- early blocks as values --------------------------------------------------

fn earlyblocks(n: i64) -> i64 {
    let r = block {
        guard n > 0 else { break -1; }
        if n > 100 { break 100; }
        n * 2
    };
    r
}

// --- inlining into a global initializer, defaults folding --------------------

fn init_helper(a: i64) -> i64 { a * 5 + 1 }
let ginit = init_helper(4);

struct Defaulted { a: i64 = 2 + 3, b: f64 = 1.0 / 4.0, c: i64 = 0 }

fn defaults() {
    let d = Defaulted { c: 9 };
    print(d.a + d.c);
    print(d.b);
}

// --- bodies that must stay real frames ---------------------------------------

// A big HOF bound to the same named nested function twice: one specialization,
// two uses, over threshold — the call survives, so its funval body still
// references this function's locals and this function must stay a real frame.
fn big_hof<F>(n: i64) -> i64 {
    var s = 0;
    for i in n { s += F(i); s *= 2; s -= 1; s += i % 3; s /= 1 + (i & 1); }
    for j in n { s += F(j) * 2; s -= j; s += 1; s *= 1 + (j & 3); }
    s
}
fn uses_big_hof() -> i64 {
    let mul = 3;
    fn scale(k: i64) -> i64 { k * mul }
    let a = big_hof(6, scale);
    let b = big_hof(7, scale);
    a + b
}

// A big capturing nested function called twice: not inlined, so the host keeps
// owning the captured local.
fn cap_host(n: i64) -> i64 {
    var t = 0;
    fn work(k: i64) {
        for i in k { t += i; t *= 2; t -= 1; t += i % 3; t /= 1 + (i & 1); }
        for j in k { t += j; t -= 1; }
    }
    work(n);
    work(n + 1);
    t
}

// A big function unwinding to outer2_op stays a call, so outer2_op must stay a frame.
fn deep_big(x: i64) -> i64 {
    var s = x;
    for i in 10 { s += i; s *= 2; s -= i / 2; s += i % 3; }
    for j in 10 { s -= j; s += 1; }
    guard s >= 0 else { return -1 from outer2_op; }
    s
}
fn outer2_op(x: i64) -> i64 { deep_big(x) + deep_big(x + 1) }

// --- inline blocks in every expression position ------------------------------

fn step(i: i64&) -> bool { i += 1; i < 5 }
fn whilecond() -> i64 {
    var i = 0;
    while step(&i) { }                               // Inlined body inside the condition.
    i
}

fn pick_op() -> i64 { 2 }
fn indexpos() -> i64 {
    let a = [10, 20, 30];
    a[pick_op()]                                        // Collapses to a constant index.
}

// --- variable-mode ADT payloads by reference through inlining ----------------

enum Node2 {
    Leaf { v: i64 },
    Pair { name: u8[] },
}
fn firstlen(n: Node2..&) -> i64 {
    match n {
        Leaf l  => l.v,
        Pair &p => p.name.len,
    }
}
fn varmode() {
    var pool: Node2..[>..] = [];
    let r: Node2..& = pool.push(Node2.Pair { name: "abc" });
    print(firstlen(r));
}

// --- base-case inlining of self-recursive functions --------------------------

struct RecNode { v: i64, l: RecNode&<u32>?, r: RecNode&<u32>? }

var rec_marks = 0;

// The first statement is a base case reading only the parameters: both
// self-call sites get it spliced in ahead of the call.
recursive fn rec_build(pool: RecNode[>..]&, depth: i64, v: i64) -> RecNode& {
    if depth == 0 { return pool.push(RecNode { v: v }); }
    let l: RecNode& = rec_build(pool, depth - 1, v * 2);
    let r: RecNode& = rec_build(pool, depth - 1, v * 2 + 1);
    return pool.push(RecNode { v: v, l: l, r: r });
}

// The `guard` spelling of the same shape; the second call has a constant
// argument, which substitutes instead of binding.
recursive fn rec_sum(n: RecNode&, d: i64) -> i64 {
    guard d > 0 else { return n.v; }
    var t = n.v;
    let l = n.l;
    if l { t += rec_sum(l, d - 1); }
    let r = n.r;
    if r { t += rec_sum(r, 0); }
    t
}

recursive fn rec_mark(n: RecNode&, d: i64) {         // Valueless base case.
    if d == 0 { return; }
    rec_marks += 1;
    let l = n.l;
    if l { rec_mark(l, d - 1); }
}

// Left alone: the test reads a local, which the call site cannot see.
recursive fn rec_local(n: RecNode&, d: i64) -> i64 {
    let stop = d <= 0;
    if stop { return n.v; }
    let l = n.l;
    guard l else { return n.v; }
    return rec_local(l, d - 1);
}

// Left alone: a mutual cycle has no self-call site to rewrite.
recursive fn rec_even(k: i64) -> i64 { if k == 0 { return 1; } return rec_odd(k - 1); }
recursive fn rec_odd(k: i64) -> i64 { if k == 0 { return 0; } return rec_even(k - 1); }

// Left alone: binding the non-fixed parameter would give the cycle a
// non-fixed local of its own (§7.8), and copy the bytes at every level.
recursive fn rec_bytes(s: u8[], k: i64) -> i64 {
    if k == 0 { return s.len; }
    return rec_bytes(s, k - 1) + 1;
}

fn recursion_op() {
    var pool: RecNode[>..] = [];
    let root: RecNode& = rec_build(&pool, 3, 1);
    print(pool.len);                                 // 15
    print(rec_sum(root, 3));                         // 32
    rec_mark(root, 3);
    print(rec_marks);                                // 3
    print(rec_local(root, 3));                       // 8
    print(rec_even(7) + rec_odd(7));                 // 0 + 1
    print(rec_bytes("hello", 3));                    // 5 + 3
}

fn main_optimize() {
    use_inlining();
    use_params();
    folding();
    casts_op();
    print(controlflow());
    limits();
    hofs_op();
    print(nested_host(4));
    shapes();
    longdistance_op();
    print(finder(5));
    print(earlyblocks(3));
    defaults();
    print(uses_big_hof());
    print(cap_host(5));
    print(outer2_op(1));
    print(whilecond() + indexpos());
    varmode();
    recursion_op();
    print(ginit + G);
    var acc = 0;
    acc += gv;
    print(acc);
    // Division by zero survives folding and is kept as a runtime abort; it
    // ends the program, so it comes last.
    let z = G - 6;                                   // 0 after propagation.
    print(100 / z);
}

// Standalone entry; ignored when this file is imported (§11.1).
fn main() { main_optimize(); }
