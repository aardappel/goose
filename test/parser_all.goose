// Full-coverage parser test: exercises every construct the parser implements.
// Parse-only; the code is not meant to be meaningful.

import util;
import sub.mod;

// --- constant expressions in array sizes ----------------------------------

let N = 4;
type ByN = int[N];
type ByExpr = flt[N * 2 + 1];
type CapByN = int[..N];

// --- trailing commas allowed in all bracketed lists ------------------------

struct Trailing {
    a: int,
    b: Pair_pa<
        int,
    >,
}

// --- struct declarations -------------------------------------------------

struct Packed_pa {
    a: i8[3],
    pad,
    b: i32,
    pad 2,
    c: int = 0,
    let d: u8 = 255,
}

struct Pair_pa<T> { a: T, b: T }

struct Holder {
    name: u8[varint],
    tail: int[>..],
}

struct WithRefs {
    r: int&,
    rel: Holder&<u16>,
    orel: Holder&<u8>?,
    opt: flt?,
    s: u8[:],
}

// --- enum declarations ---------------------------------------------------

enum Shape {
    Circle { r: flt },
    Rect { w: flt, h: flt },
    Point,
}

enum Color { Red, Green, Blue }

enum Tree<T> {
    Leaf { v: T },
    Branch { l: Tree<T>..&<u32>, r: Tree<T>..&<u32> },
}

// --- type aliases covering the whole postfix type zoo --------------------

type Str = u8[];
type CompactStr = u8[varint];
type Fixed3 = flt[3];
type Limited = int[..16];
type LimitedVar = int[..];
type Grow = int[>..];
type GrowShrink = int[>..<];
type SliceI = int[:];
type RefI = int&;
type RelRef = Holder&<u32>;
type OptI = int?;
type ShapeVar = Shape..;
type CircleT = Shape.Circle;
type Fn0 = fn;
type Fn1 = fn(int);
type Fn2 = fn(int, flt) -> bool;
type FnMulti = fn(int) -> (int, bool);
type Grouped = (Shape..&<varint>)[varint];
type NestedGen = Pair_pa<Pair_pa<int>>;
type DeepArray = Pair_pa<int>[4][:];

// --- globals -------------------------------------------------------------

let gconst = 42;
var gcounter_pa: int = 0;
let gname_pa: u8[] = "goose";
var gpool_pa: Shape..[>..] = [];
reusable var gslots: Slot2[>..] = [];

struct Slot2 { v: int = 0 }

// --- functions: headers --------------------------------------------------

fn no_args() { }
fn one(a: int) -> int { a }
fn defaults_and_var(var a: int, b: flt) -> flt { a++; a as flt + b }
fn untyped_generic(a, b) { print(a); print(b); }
fn typed_generic<T>(a: T, b: T) -> T { if a == b { a } else { b } }
fn bounded<F: fn(int)>(n: int) { F(n); }
fn bare_bound<F>(n: int) { F(n); }
fn multi_ret(a: int) -> int, bool { return a, a > 0; }
fn multi_ret_nonfixed(n: int) -> u8[], int { return "x", n; }
recursive fn fact(n: int) -> int { if n <= 1 { 1 } else { n * fact(n - 1) } }
thread_fn worker_pa(id: int) {
    qput(id);
    let got = qget<int>();
    let polled, ok = qpoll<int>();
    print(got + polled);
    print(ok);
}

// --- expressions ---------------------------------------------------------

fn literals() {
    let a = 123;
    let b = 0xDEADBEEF;
    let c = 18446744073709551615;
    let d = 1.5;
    let e = 2.5e-3;
    let f = 'x';
    let g = '\n';
    let h = "hello\tworld\n";
    let i = true;
    let j = false;
    let k = 255;
    let l = 0.5;
    let m: int[5] = [1, 2, 3, 4, 5];
    let n: int[100] = [0; 100];
    let o: int[>..] = [];
    let hf = 0x1.8p3;
    let hf2 = 0x1p-2;
    let esc = "nul:\0 hex:\x41\x7f";
    let escc = '\x0a';
    let trail = [
        1,
        2,
    ];
    print(a + b + c + f + g + k);
    print(d + e + hf + hf2);
    print(l);
    print(h.len + m.len + n.len + o.len);
    print(esc.len + escc + trail.len);
    print(i && !j);
}

fn operators(a: int, b: int) -> int {
    let arith = a + b - a * b / (a % b);
    let bits = ~a & b | a ^ b << 2 >> 1;
    let cmp = a < b || a <= b || a > b || a >= b || a == b || a != b;
    let logic = !cmp && a + 1 < b || false;
    let neg = - -a;
    let casts = (a as u8) as! i16 as flt;
    print(casts + 0.5);
    if logic { arith + bits + neg } else { arith - bits }
}

fn assignments(var x: int) {
    var y = x;
    y = 1;
    y += 2;
    y -= 3;
    y *= 4;
    y /= 5;
    y %= 6;
    y &= 7;
    y |= 8;
    y ^= 9;
    y++;
    y--;
    x = y;
    let p = Pair_pa<int> { a: 1, b: 2 };
    var q = p;
    q.a = 3;
    var arr = [1, 2, 3];
    arr[0] = arr[1] + arr[2];
    print(q.b + arr[0] + x);
}

fn struct_literals() {
    let named = Packed_pa { a: [1, 2, 3], b: 4, c: 5, d: 6 };
    let defaulted = Packed_pa { a: [1, 2, 3], b: 4 };
    let positional = Slot2 { 1 };
    let empty = Slot2 {};
    let inferred = Pair_pa { a: 1, b: 2 };
    let generic = Pair_pa<flt> { a: 1.0, b: 2.0 };
    let variant = Shape.Circle { r: 1.0 };
    let variant2 = Shape.Rect { 3.0, 4.0 };
    let multiline = Packed_pa {
        a: [1, 2, 3],
        b: 4,
        c: 5,
    };
    print(multiline.c);
    let nested = Pair_pa<Pair_pa<int>> { a: Pair_pa<int> { a: 1, b: 2 }, b: Pair_pa<int> { 3, 4 } };
    print(named.c + defaulted.c + positional.v + empty.v);
    print(inferred.a + nested.a.b);
    print(generic.a);
    print(variant.r);
    print(variant2.w);
}

fn references_and_slices(data: int[:]) -> int {
    var local = [1, 2, 3, 4, 5];
    let r = &local[2];
    let whole = &local;
    let s1 = local[1..3];
    let s2 = local[..];
    let s3 = local[2..];
    let s4 = local[..^1];
    let s5 = local[^2..^0];
    let ss = s1[..1];
    let copied = r;          // References are transparent: copies the pointee.
    print(copied + data[0] + data.len);
    print(s2.len + s3.len + s4.len + s5.len + ss.len);
    print(whole.len);
    return local[0];
}

fn optionals(o: int?) -> int {
    if o { o } else { 0 }
}

fn calls_and_ufcs() {
    let v1 = Vec2 { x: 1.0, y: 2.0 };
    let v2 = Vec2 { x: 3.0, y: 4.0 };
    let v3 = add2(v1, v2);
    let v4 = v1.add2(v2);
    print(v3.x + v4.y);
    let t = typed_generic<int>(1, 2);
    let u = typed_generic(1.0, 2.0);
    print(t);
    print(u);
    var b: int[>..<] = [];
    b.push(1);
    b.append([2, 3]);
    b.resize(10, 0);
    print(b.pop());
    print(b.len);
    b.clear();
}

fn function_values() {
    // Trailing-block calls are block-ended statements: no semicolon needed.
    bounded(1) { print(it) }
    bounded(2) { x => print(x) }
    bounded(3) { x: int => print(x) }
    fn named(v: int) { print(v) }
    bounded(4, named);
    bare_bound(5) {
        let doubled = it * 2;
        print(doubled)
    }
    // But as an operand inside a larger statement they are plain expressions.
    let r = one(6) + 0;
    print(r);
}

fn nested_functions(outer: int) -> int {
    var acc = 0;
    fn add_in(v: int) { acc += v; }
    add_in(outer);
    add_in(acc);
    return acc;
}

// --- control flow --------------------------------------------------------

fn control(n: int) -> int {
    let v = if n > 0 { 1 } else if n < 0 { -1 } else { 0 };
    if v == 1 { print(1); } else { print(2); }
    if v == 0 { print(3); }

    var i = 0;
    while i < n { i++; }

    loop {
        i--;
        if i <= 0 { break; }
        continue;
    }

    for j in 0..10 { print(j); }
    for j in n { print(j); }
    let arr = [1, 2, 3];
    for x in arr { print(x); }
    for &x in arr { print(x); }
    for x, j in arr { print(x + j); }
    for &x, j in arr { print(x + j); }

    let blocked = block {
        guard n != 0 else { break 0; }
        guard n > 0 else { break 0; }
        n * 2
    };
    block {
        guard n >= 0;
        print(n);
    }
    return blocked + v;
}

fn guards(o: int?, n: int) -> int {
    guard o else { return -1; }
    guard n > 0 else { return 0; }
    o + n
}

fn matches(s: Shape, sv: Shape.., n: int, c: int) -> flt {
    let area = match s {
        Circle cr => cr.r * cr.r * 3.14159,
        Rect r => r.w * r.h,
        Point => 0.0,
    };
    let area2 = match sv {
        Circle cr => cr.r,
        _ => 1.0,
    };
    let cls = match n {
        0 => "zero",
        1..10 => "small",
        -5..-1 => "negative",
        _ => "big",
    };
    let ch = match c { 'a'..'z' => 1, '0' => 2, _ => 0 };
    print(cls);
    print(ch);
    area + area2
}

// --- case functions (match as an overload set) ---------------------------

fn area(c: Shape.Circle) -> flt { c.r * c.r * 3.14159 }
fn area(r: Shape.Rect) -> flt { r.w * r.h }
fn area(p: Shape.Point) -> flt { 0.0 }

fn dispatch(s: Shape) -> flt { area(s) }

// --- return forms --------------------------------------------------------

fn early(n: int) -> int {
    if n < 0 { return 0; }
    return n;
}

fn deep_pa(n: int) -> int {
    if n == 0 { return -1 from outer; }
    n
}

fn outer(n: int) -> int {
    return deep_pa(n) + 1;
}

fn returns_nothing(n: int) {
    if n == 0 { return; }
    print(n);
}

// --- reusable pools_pa ------------------------------------------------------

fn pools_pa() {
    reusable var slots: Slot2[>..] = [];
    let i = slots.alloc_index(Slot2 { v: 1 });
    let r = slots.alloc_ref(Slot2 { v: 2 });
    slots.free(i);
    print(r.v);
}

// --- misc statements -----------------------------------------------------

fn misc() {
    let a, b = multi_ret(7);
    let x, y = 1, 2;
    var unassigned: int;
    unassigned = a + x + y;
    let shadow = 1;
    if shadow == 1 {
        let shadow = 2;
        print(shadow);
    }
    print(unassigned);
    print(b);
    let ud = unsigned_div(10, 3);
    let um = unsigned_mod(10, 3);
    let us = unsigned_shr(10, 1);
    let ul = unsigned_less(1, 2);
    print(ud + um + us);
    print(ul);
    assert(a > 0);
    gcounter_pa += 1;
    let spawned = thread_spawn(worker_pa, 1);
    thread_wait(spawned);
    print(hardware_threads());
}

fn edge_cases() {
    // Struct literals in scrutinee position need parens (the Rust rule).
    if (Slot2 { v: 1 }).v == 1 { print(1); }
    while (Slot2 { v: 0 }).v != 0 { break; }
    // Nested generic arguments at a call site: the >> split.
    let p = Pair_pa<int> { a: 1, b: 2 };
    let q = typed_generic<Pair_pa<int>>(p, p);
    print(q.a);
    var lim: int[..8] = [1, 2];
    print(lim.cap + lim.len);
    let sl = lim[..];
    for x in sl { print(x); }
}

fn main() {
    edge_cases();
    literals();
    print(operators(3, 4));
    assignments(5);
    struct_literals();
    let arr = [9, 8, 7];
    print(references_and_slices(arr));
    print(optionals(&arr[0]));
    calls_and_ufcs();
    function_values();
    print(nested_functions(2));
    print(control(5));
    print(guards(&arr[1], 1));
    print(matches(Shape.Point {}, Shape.Circle { r: 2.0 }, 5, 'q'));
    print(dispatch(Shape.Rect { w: 2.0, h: 3.0 }));
    print(early(1) + outer(2));
    returns_nothing(4);
    pools_pa();
    misc();
}
