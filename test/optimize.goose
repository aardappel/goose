// Optimizer coverage: inlining, constant folding, and propagation shapes.
// Not a meaningful program; it exercises every optimizer path and must keep
// typechecking (and meaning the same thing) at every -O level.

// --- inlining fodder ---------------------------------------------------------

fn tiny_add(a: int, b: int) -> int { a + b }         // Multi-use, below NC.
fn tiny_flt(x: flt) -> flt { x * 2.0 }
fn once_used(n: int) -> int {                        // Single use, large body.
    var s = 0;
    for i in n { s += i * i; }
    if s > 100 { s = s - 100; }
    while s > 17 { s -= 17; }
    s
}
fn medium(v: int, w: int) -> int {                   // Lands in NCU territory.
    let a = v * 2 + w;
    let b = a - v / 3;
    if a > b { a } else { b }
}
fn big_not_inlined(n: int) -> int {                  // Over every threshold.
    var s = 1;
    for i in n { s += i; s *= 3; s -= i / 2; s += i % 5; }
    for j in n { s += j * j; s -= j * 2; s += 1; }
    if s < 0 { s = 0 - s; }
    while s > 1000 { s /= 7; s -= 3; }
    s
}
fn returns_early(n: int) -> int {                    // Return statements in loops.
    if n < 0 { return -1; }
    for i in n { if i == 3 { return i * 10; } }
    n
}
fn void_helper(v: int) {                             // Void result + bare guard.
    guard v > 0;
    print(v);
}
fn var_param(var x: int) -> int { x += 1; x * 2 }    // Written param: no const subst.
fn two_rets(a: int) -> int, int { return a, a + 1; } // Multi-value: never inlined.
recursive fn fact_op(n: int) -> int { if n <= 1 { 1 } else { n * fact_op(n - 1) } }

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

fn bump(r: int&) { r += 1; }                         // Reference binds; writes pointee.
fn doubled(v: int) -> int { v * 2 }
fn total_op(xs: int[:]) -> int {
    var s = 0;
    for x in xs { s += x; }
    s
}
fn takes_string(s: u8[]) -> int { s.len }            // Variable-size by value.
fn grow(v: int[>..]&) { v.push(1); v.push(2); }      // Growing through a reference.
fn make_str(n: int) -> u8[>..] {                     // Nonfixed return (NRVO shape).
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
    var v: int[>..] = [];
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
    let i = 3.0 as int;                              // Integral: folds.
    let f2 = 7 as flt;
    let g3 = 2.5 as! int;                            // as! truncation folds.
    let h2 = 1.5 as f32;                             // Exact in f32: folds.
    let big = 10000000000000000000.5 as! int;        // Out of range: left alone.
    print(s.b + s.w + i + g3 + big);
    print(f2 + (h2 as flt) + (s.f as flt));
}

// --- control-flow folding ----------------------------------------------------

fn controlflow() -> int {
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
    let cap: int[..4] = [1, 2];
    print(cap.cap + cap.len);                        // .cap folds; .len stays dynamic.
}

// --- HOFs, function values, captures -----------------------------------------

fn apply<F>(x: int) -> int { F(x) }
fn twice<F>(x: int) -> int { F(F(x)) }
fn bigger_hof<F>(n: int) -> int {
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

fn nested_host(n: int) -> int {
    fn helper(k: int) -> int { k * 3 }               // Nested, no captures.
    var t = 0;
    fn addt(k: int) { t += k; }                      // Captures t.
    addt(helper(n));
    addt(2);
    t
}

// --- case functions and match binders ----------------------------------------

enum Shape_op {
    Circle { r: flt },
    Rect { w: flt, h: flt },
    Point,
}

fn area_op(c: Shape_op.Circle) -> flt { c.r * c.r * 3.14159 }
fn area_op(r: Shape_op.Rect) -> flt { r.w * r.h }
fn area_op(p: Shape_op.Point) -> flt { 0.0 }

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

fn deep_fail(x: int) -> int {
    guard x >= 0 else { return -1 from outer_op; }
    x * 2
}
fn outer_op(x: int) -> int {
    let v = deep_fail(x);                            // Callee unwinds to outer_op.
    v + 1
}
fn longdistance_op() {
    print(outer_op(5));
    print(outer_op(0 - 5));
}

// --- function-value returns exit the defining function -----------------------

fn each<F>(n: int) { for i in n { F(i); } }
fn finder(n: int) -> int {
    each(n) { if it == 2 { return it * 100; } };     // Exits finder, through each.
    -1
}

// --- early blocks as values --------------------------------------------------

fn earlyblocks(n: int) -> int {
    let r = block {
        guard n > 0 else { break -1; }
        if n > 100 { break 100; }
        n * 2
    };
    r
}

// --- inlining into a global initializer, defaults folding --------------------

fn init_helper(a: int) -> int { a * 5 + 1 }
let ginit = init_helper(4);

struct Defaulted { a: int = 2 + 3, b: flt = 1.0 / 4.0, c: int = 0 }

fn defaults() {
    let d = Defaulted { c: 9 };
    print(d.a + d.c);
    print(d.b);
}

// --- bodies that must stay real frames ---------------------------------------

// A big HOF bound to the same named nested function twice: one specialization,
// two uses, over threshold — the call survives, so its funval body still
// references this function's locals and this function must stay a real frame.
fn big_hof<F>(n: int) -> int {
    var s = 0;
    for i in n { s += F(i); s *= 2; s -= 1; s += i % 3; s /= 1 + (i & 1); }
    for j in n { s += F(j) * 2; s -= j; s += 1; s *= 1 + (j & 3); }
    s
}
fn uses_big_hof() -> int {
    let mul = 3;
    fn scale(k: int) -> int { k * mul }
    let a = big_hof(6, scale);
    let b = big_hof(7, scale);
    a + b
}

// A big capturing nested function called twice: not inlined, so the host keeps
// owning the captured local.
fn cap_host(n: int) -> int {
    var t = 0;
    fn work(k: int) {
        for i in k { t += i; t *= 2; t -= 1; t += i % 3; t /= 1 + (i & 1); }
        for j in k { t += j; t -= 1; }
    }
    work(n);
    work(n + 1);
    t
}

// A big function unwinding to outer2_op stays a call, so outer2_op must stay a frame.
fn deep_big(x: int) -> int {
    var s = x;
    for i in 10 { s += i; s *= 2; s -= i / 2; s += i % 3; }
    for j in 10 { s -= j; s += 1; }
    guard s >= 0 else { return -1 from outer2_op; }
    s
}
fn outer2_op(x: int) -> int { deep_big(x) + deep_big(x + 1) }

// --- inline blocks in every expression position ------------------------------

fn step(i: int&) -> bool { i += 1; i < 5 }
fn whilecond() -> int {
    var i = 0;
    while step(&i) { }                               // Inlined body inside the condition.
    i
}

fn pick_op() -> int { 2 }
fn indexpos() -> int {
    let a = [10, 20, 30];
    a[pick_op()]                                        // Collapses to a constant index.
}

// --- variable-mode ADT payloads by reference through inlining ----------------

enum Node2 {
    Leaf { v: int },
    Pair { name: u8[] },
}
fn firstlen(n: Node2..&) -> int {
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

fn main() {
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
    print(ginit + G);
    var acc = 0;
    acc += gv;
    print(acc);
    // Division by zero survives folding and is kept as a runtime abort; it
    // ends the program, so it comes last.
    let z = G - 6;                                   // 0 after propagation.
    print(100 / z);
}
