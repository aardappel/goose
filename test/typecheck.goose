// Full-coverage typechecker test: exercises the semantic rules the parser
// tests don't reach. Not a meaningful program; it just has to typecheck.

// --- storage types, packing, defaults ---------------------------------------

struct Packed {
    a: i8,
    b: u16,
    pad,
    c: i32,
    pad 3,
    d: u64,
    let e: u8 = 7,
    f: f32 = 0.5,
}

fn storage() {
    var p = Packed { a: -5, b: 1000, c: -70000, d: 18446744073709551615 };
    let widened = p.a + p.b + p.c + p.d + p.e;   // All reads widen to int.
    p.a = -128;          // Literal fit into i8.
    p.b = 65535;
    p.c = 0x7FFFFFFF;
    p.d = 1;             // 64-bit storage takes any int.
    let x = 300;
    p.c = x as i32;      // Non-literal ints need `as` to narrow.
    print(widened + p.c);
    // f32 math lives within one expression: loads of f32 storage compute in
    // 32 bits, literals adapt, and any variable binding widens to flt.
    p.f = p.f * 2.0 + 0.25;
    let widerf = p.f + 0.5;
    print(widerf);
    print(p.f as flt + 1.0);
}

// f32 widening at generic and untyped bindings and inferred returns.
struct F2 { a: f32, b: f32 }
fn samef<T>(x: T, y: T) -> bool { x == y }
fn pickf(a, b) -> flt { if a == a { a } else { b } }
fn half(f: F2&) -> flt { f.a * 0.5 }

fn f32widen() {
    var f = F2 { a: 1.5, b: 2.5 };
    print(samef(f.a, 1.0));           // T binds flt; the f32 promotes in.
    print(pickf(f.a, f.b) + half(&f));  // Untyped params widen too.
}

// --- varint fields and compact strings --------------------------------------

struct Compact {
    tag: varint,
    name: u8[varint],
}

fn varints() {
    let c = Compact { tag: -3, name: "abc" };
    let t = c.tag + c.name.len;          // varint reads widen to int.
    print(t);
    let tiny: u8[u8] = "hi";             // Explicit length-field storage type.
    print(tiny.len);
}

// --- elementwise math (§6.1) ------------------------------------------------

struct Float3 { x: flt, y: flt, z: flt }

fn elementwise() {
    let a = Float3 { 1.0, 2.0, 3.0 };
    let b = Float3 { 4.0, 5.0, 6.0 };
    let c = a + b;
    let d = c * a - b;
    print(d.x + d.y + d.z);
    let ia = [1, 2, 3];
    let ib = [4, 5, 6];
    let ic = ia + ib;                    // Fixed int arrays, memberwise.
    print(ic[0] + ic[1] + ic[2]);
}

// --- equality and comparison ------------------------------------------------

fn equality() {
    let v1 = Float3 { 1.0, 2.0, 3.0 };
    let v2 = Float3 { 1.0, 2.0, 3.0 };
    print(v1 == v2);
    print([1, 2] != [2, 1]);
    let s: u8[] = "xy";
    let t: u8[] = "xy";
    print(s == t);
    var a = 1;
    let r1 = &a;
    let r2 = &a;
    print(r1 == r2);                     // Transparent: compares the pointees.
}

// --- references: creation, writes, provenance -------------------------------

fn add_through(r: int&, v: int) { r += v; }

fn refs() {
    var a = 1;
    let r = &a;
    r = 5;                               // Transparent: writes the pointee.
    r += 2;
    r++;
    let readback = r;                    // And reads it (a plain int copy).
    add_through(&a, 10);                 // Writable provenance: a is var.
    var arr = [1, 2, 3];
    for &x in arr { x = x * 2; }         // The mutation idiom.
    print(a + arr[0] + readback);
    var opt: int? = &a;
    guard opt else { return; }
    print(opt);
    var rebound: int& = &arr[0];
    rebound .= &arr[1];                  // .= rebinds the reference itself.
    rebound = 9;
    print(rebound + arr[1]);
}

// --- optionals and flow narrowing -------------------------------------------

fn narrowing(o: int?, p: int?) -> int {
    if o && o > 0 { return o; }
    guard p else { return -1; }
    let v = p + 0;                       // Narrowed: reads the pointee.
    if !o { return v; }
    o + v
}

fn assert_narrow(o: int?) -> int {
    assert(o);                           // Narrows o for the rest of the block.
    o
}

struct OptBox { o: int&? }

fn transparency() {
    var g: int[>..] = [];
    push(g, 1);                    // Members are functions: free-call form.
    g.len.print();                 // ...and builtins work via UFCS.
    var a = 5;
    let r = &a;
    add_through(&r, 1);            // &r re-derives the same reference.
    print(a);
    var b = OptBox { };            // Optional field defaults to null.
    b.o .= &a;                     // Rebind a field.
    b.o .= null;
    var o: int? = null;
    o .= &a;
    print(o);                      // Narrowed by the non-null rebind.
    if o != null { print(o + 1); }
}

fn nulls(var o: int?) -> int {
    if o == null { return -1; }
    let v = o;                           // Narrowed by the null test.
    o .= null;                           // Rebinding to null kills narrowing.
    guard o != null else { return v; }
    o + v
}

// --- limited arrays: refs survive pops (§5.3) -------------------------------

fn limited() {
    var lim: int[..4] = [1, 2, 3];
    let r = &lim[1];
    lim.pop();
    lim.push(9);                         // Type-safe reuse: r stays valid.
    print(r + lim.cap + lim.len);
    var open: int[..] = [1, 2, 3, 4];    // Construction-time capacity.
    open.resize(2);
    open.resize(4, 0);
    open.clear();
    print(open.len);
}

// --- grow-only arrays: interior refs/slices stay valid (§5.1) ---------------

fn growonly() {
    var g: flt[>..] = [];
    let first: flt& = g.push(1.0);       // push returns a ref to the element.
    let r = &g[0];
    let s = g[0..1];
    g.append([2.0, 3.0]);
    g.append([0.0; 3]);                  // (resize does not exist on grow-only.)
    print(first + r + s[0] + g.len as flt);
}

fn push_through(v: flt[>..]&) { v.push(4.0); }

fn grow_via_ref() {
    var g: flt[>..] = [1.0];
    push_through(&g);
    print(g.len);
}

// --- reusable pools (§5.4) --------------------------------------------------

struct Slot { v: int = 0, next: int = -1 }

reusable var gpool: Slot[>..] = [];

fn use_pool(pool: Slot[>..]&) -> int {
    let i = pool.alloc_index(Slot { v: 1 });
    let r: Slot& = pool.alloc_ref(Slot { v: 2 });
    r.v = 3;
    pool.free(i);
    r.v
}

fn pools() {
    reusable var local: Slot[>..] = [];
    print(use_pool(&local));
    print(use_pool(&gpool));
}

// --- relative references within one pool (§3.9, A.2) ------------------------

enum Sexp {
    Sym  { name: u8[varint] },
    List { kid: Sexp..&<varint>? },
}

var spool: Sexp..[>..] = [];

fn build_tree() {
    // push returns a reference to the new element: build children first,
    // link parents with tiny relative references (A.2).
    let leaf: Sexp..& = spool.push(Sexp.Sym { name: "leaf" });
    spool.push(Sexp.List { kid: leaf });   // Same root: relative store fits.
    for &s in spool {
        match s {
            Sym &sy => print(sy.name.len),     // &-binder: variable mode only.
            // Relative-reference payloads bind by reference (copies of values
            // containing them are rejected).
            List &l => if l.kid { print(1); } else { print(0); },
        }
    }
}

// --- variable-size elements: sequential arrays ------------------------------

fn words(text: u8[:]) -> u8[][>..] {
    var out: u8[][>..] = [];
    var i = 0;
    while i < text.len {
        let start = i;
        while i < text.len && text[i] != ' ' { i++; }
        out.push(text[start..i]);
        while i < text.len && text[i] == ' ' { i++; }
    }
    return out;
}

fn sequential() {
    let ws = words("feed the goose");
    var n = 0;
    for &w, i in ws { n += w.len + i; }
    print(n);
}

// --- generic structs and enums, nesting, explicit args ----------------------

struct Pair<T> { a: T, b: T }

enum Opt<T> { None, Some { v: T } }

fn generic_data() {
    let p = Pair { a: 1, b: 2 };                  // Inferred from fields.
    let q = Pair<Pair<int>> { a: p, b: Pair<int> { 3, 4 } };
    print(q.a.a + q.b.b);
    let s: Opt<int> = Opt<int>.Some { v: 5 };
    let n: Opt<int> = Opt<int>.None {};
    let got = match s { Some x => x.v, None => 0 };
    let got2 = match n { Some x => x.v, _ => -1 };
    print(got + got2);
}

fn first<T>(xs: T[:]) -> T { xs[0] }
fn same<T>(a: T, b: T) -> bool { a == b }
fn pick(a, b) { print(a); print(b); }             // Untyped params: generic.

// A nested function using the enclosing function's generic.
fn outer2<T>(x: T) -> T {
    fn inner(y: T) -> T { y }
    inner(x)
}

// Recursive generics: the back edge reuses the same specialization.
recursive fn depth2<T>(x: T, n: int) -> int {
    if n <= 0 { 0 } else { depth2(x, n - 1) + 1 }
}

// Forwarding a function value down through another HOF.
fn apply2<F>(x: int) { F(x); }
fn forward2<F>(x: int) { apply2(x, F); }

fn generic_fns() {
    let arr = [7, 8, 9];
    print(first(arr));                            // T inferred through slice.
    let f = [1.5, 2.5];
    print(first(f));
    print(same(1, 2));
    print(same("a", "b"));
    pick(1, 2.5);
    print(first<int>([4, 5]));                    // Explicit type argument.
    print(outer2(5));
    print(outer2(1.5));
    print(depth2(true, 3));
    forward2(7) { print(it * 3) }
}

// --- function values and HOFs (§7.6) ----------------------------------------

fn repeat<F: fn(int)>(n: int) { for i in n { F(i); } }
fn fold<F>(xs: int[:], var acc: int) -> int {
    for x in xs { acc = F(acc, x); }
    return acc;
}

fn hofs() {
    repeat(3) { print(it) }
    let xs = [1, 2, 3];
    let sum = fold(xs, 0) { a, x => a + x };
    print(sum);
    fn double(v: int) { print(v * 2) }
    repeat(2, double);
    var captured = 0;
    fn bump(v: int) { captured += v; }
    bump(5);
    repeat(2) { captured += it; }
    print(captured);
}

// Plain return inside a function value exits the named function (§7.6).
fn find_gt(xs: int[:], limit: int) -> int {
    repeat(xs.len) {
        if xs[it] > limit { return xs[it]; }
    }
    return -1;
}

// --- multiple returns, nonfixed returns, NRVO shapes ------------------------

fn minmax(a: int, b: int) -> int, int {
    if a < b { return a, b; }
    return b, a;
}

fn labelled(n: int) -> u8[], int { return "num", n }

fn multi() {
    let lo, hi = minmax(4, 2);
    let name, v = labelled(7);
    print(lo + hi + v + name.len);
    let a, b = minmax(1, 2);
    print(find_gt([a, b], 1));
}

// --- case functions with references (§8.2) ----------------------------------

enum Node { Num { v: flt }, Neg { of: Node..&<u16>? } }

var npool: Node..[>..] = [];

recursive fn eval(n: Node.Num&) -> flt { n.v }
recursive fn eval(n: Node.Neg&) -> flt {
    let of = n.of;                       // Narrowing tracks locals, so bind it.
    guard of else { return 0.0; }
    -eval(of)
}

fn casedispatch() {
    let num: Node..& = npool.push(Node.Num { v: 2.0 });
    npool.push(Node.Neg { of: num });
    var total = 0.0;
    for &n in npool { total = total + eval(n); }
    print(total);
}

// --- fixed-mode ADTs: in-place variant replacement (§3.5) -------------------

enum State { Idle, Busy { job: int }, Done { code: i16 } }

fn fixedmode() {
    var s: State = State.Idle {};
    s = State.Busy { job: 3 };
    let v = match s {
        Idle => 0,
        Busy b => b.job,
        Done d => d.code,
    };
    s = State.Done { code: -1 };
    print(v);
    let c = State.Idle;                  // Payload-less variant constant.
    print(match c { Idle => 1, _ => 0 });
}

// --- match over integers ----------------------------------------------------

fn intmatch(n: int) {
    let s = match n {
        -9..0    => "negative",
        0        => "zero",
        1..10    => "small",
        'a'..'z' => "letter",
        _        => "big",
    };
    print(s);
}

// --- guards, blocks, loop values --------------------------------------------

fn guards_blocks(n: int) -> int {
    let picked = loop {
        if n > 0 { break n; }
        break 1;
    };
    let blocked = block {
        guard n != 42 else { break 0; }
        picked * 2
    };
    var i = 0;
    while true {
        guard i < 3;
        i++;
    }
    for j in 2..5 {
        guard j != 3 else { continue; }
        print(j);
    }
    return blocked + i;
}

// --- long-distance returns (§7.9) -------------------------------------------

fn inner_fail(n: int) -> int {
    if n < 0 { return -1 from process; }
    n * 2
}

fn middle(n: int) -> int { inner_fail(n) + 1 }

fn process(n: int) -> int {
    return middle(n);
}

// --- globals, writability into globals, struct-stored refs ------------------

var gcounter = 0;
let gname: u8[] = "goose";
var gvec = Float3 { 0.0, 0.0, 0.0 };

var gint = 5;
struct GHolder { r: int& }
var gheld = GHolder { r: &gint };        // Global-rooted ref into a global.

fn globals() {
    gcounter += gname.len;
    gvec = gvec + Float3 { 1.0, 1.0, 1.0 };
    gheld.r .= &gcounter;                // Rebind: same-depth store, fine.
    gheld.r = 7;                         // Transparent: writes gcounter.
    print(gheld.r + gcounter);
}

// --- slices: bounds forms, re-slicing, writable slices ----------------------

fn fill(s: int[:], v: int) { for i in s.len { s[i] = v; } }

fn slices() {
    var data = [1, 2, 3, 4, 5];
    let s1 = data[1..3];
    let s2 = data[..^1];
    let s3 = data[^2..];
    let s4 = s1[..1];
    fill(data[..], 9);                   // Writable: data is var.
    print(s1[0] + s2.len + s3.len + s4.len + data[0]);
    let str = "static";
    print(str[1..3].len);                // Reads through a literal slice.
}

// --- shadowing and definite assignment --------------------------------------

fn shadowing(n: int) {
    var acc: int;
    if n > 0 { acc = 1; } else { acc = 2; }
    print(acc);
    let x = 1;
    if x == 1 {
        let x = 2.5;
        print(x);
    }
    print(x);
}

fn main_typecheck() {
    storage();
    f32widen();
    varints();
    elementwise();
    equality();
    refs();
    print(narrowing(&gcounter, &gcounter));
    print(assert_narrow(&gcounter));
    transparency();
    print(nulls(&gcounter));
    print(nulls(null));
    limited();
    growonly();
    grow_via_ref();
    pools();
    build_tree();
    sequential();
    generic_data();
    generic_fns();
    hofs();
    multi();
    casedispatch();
    fixedmode();
    intmatch(5);
    print(guards_blocks(21));
    print(process(3) + process(-3));
    globals();
    slices();
    shadowing(1);
}

// Standalone entry; ignored when this file is imported (§11.1).
fn main() { main_typecheck(); }
