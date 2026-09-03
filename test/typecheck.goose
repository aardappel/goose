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
    // Narrow reads widen implicitly into an i64 expression; u64 needs a cast.
    let widened = p.a as i64 + p.b + p.c + p.d as! i64 + p.e;
    p.a = -128;          // Literal fit into i8.
    p.b = 65535;
    p.c = 0x7FFFFFFF;
    p.d = 1;
    let x = 300;
    p.c = x as i32;      // Non-literal narrowing needs `as`.
    print(widened + p.c);
    // f32 math stays 32-bit: loads, literals, and locals all keep the type.
    p.f = p.f * 2.0 + 0.25;
    let widerf = p.f + 0.5;
    print(widerf);
    print(p.f as f64 + 1.0);
}

// f32 at generic and untyped bindings, widening at f64 returns.
struct F2 { a: f32, b: f32 }
fn samef<T>(x: T, y: T) -> bool { x == y }
fn pickf(a, b) -> f64 { if a == a { a } else { b } }
fn half(f: F2&) -> f64 { f.a * 0.5 }

fn f32widen() {
    var f = F2 { a: 1.5, b: 2.5 };
    print(samef(f.a, 1.0));             // T binds f32; the literal adapts in.
    print(pickf(f.a, f.b) + half(&f));  // f32 results widen into the f64 returns.
}

// --- varint fields and compact strings --------------------------------------

struct Compact {
    tag: varint,
    name: u8[varint],
}

fn varints() {
    let c = Compact { tag: -3, name: "abc" };
    let t = c.tag + c.name.len;          // varint reads widen to i64.
    print(t);
    let tiny: u8[u8] = "hi";             // Explicit length-field storage type.
    print(tiny.len);
}

// --- elementwise math (§6.1) ------------------------------------------------

struct Float3 { x: f64, y: f64, z: f64 }

fn elementwise() {
    let a = Float3 { 1.0, 2.0, 3.0 };
    let b = Float3 { 4.0, 5.0, 6.0 };
    let c = a + b;
    let d = c * a - b;
    print(d.x + d.y + d.z);
    let ia = [1, 2, 3];
    let ib = [4, 5, 6];
    let ic = ia + ib;                    // Fixed i64 arrays, memberwise.
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

fn add_through(r: i64&, v: i64) { r += v; }

fn refs() {
    var a = 1;
    let r = &a;
    r = 5;                               // Transparent: writes the pointee.
    r += 2;
    r++;
    let readback = r;                    // And reads it (a plain i64 copy).
    add_through(&a, 10);                 // Writable provenance: a is var.
    var arr = [1, 2, 3];
    for &x in arr { x = x * 2; }         // The mutation idiom.
    print(a + arr[0] + readback);
    var opt: i64? = &a;
    guard opt else { return; }
    print(opt);
    var rebound: i64& = &arr[0];
    rebound .= &arr[1];                  // .= rebinds the reference itself.
    rebound = 9;
    print(rebound + arr[1]);
}

// --- optionals and flow narrowing -------------------------------------------

fn narrowing(o: i64?, p: i64?) -> i64 {
    if o && o > 0 { return o; }
    guard p else { return -1; }
    let v = p + 0;                       // Narrowed: reads the pointee.
    if !o { return v; }
    o + v
}

fn assert_narrow(o: i64?) -> i64 {
    assert(o);                           // Narrows o for the rest of the block.
    o
}

struct OptBox { o: i64&? }

fn transparency() {
    var g: i64[>..] = [];
    push(g, 1);                    // Members are functions: free-call form.
    g.len.print();                 // ...and builtins work via UFCS.
    var a = 5;
    let r = &a;
    add_through(&r, 1);            // &r re-derives the same reference.
    print(a);
    var b = OptBox { };            // Optional field defaults to null.
    b.o .= &a;                     // Rebind a field.
    b.o .= null;
    var o: i64? = null;
    o .= &a;
    print(o);                      // Narrowed by the non-null rebind.
    if o != null { print(o + 1); }
}

fn nulls(var o: i64?) -> i64 {
    if o == null { return -1; }
    let v = o;                           // Narrowed by the null test.
    o .= null;                           // Rebinding to null kills narrowing.
    guard o != null else { return v; }
    o + v
}

// --- limited arrays: refs survive pops (§5.3) -------------------------------

fn limited() {
    var lim: i64[..4] = [1, 2, 3];
    let r = &lim[1];
    lim.pop();
    lim.push(9);                         // Type-safe reuse: r stays valid.
    print(r + lim.cap + lim.len);
    var open: i64[..] = [1, 2, 3, 4];    // Construction-time capacity.
    open.resize(2);
    open.resize(4, 0);
    open.clear();
    print(open.len);
}

// --- grow-only arrays: interior refs/slices stay valid (§5.1) ---------------

fn growonly() {
    var g: f64[>..] = [];
    let first: f64& = g.push(1.0);       // push returns a ref to the element.
    let r = &g[0];
    let s = g[0..1];
    g.append([2.0, 3.0]);
    g.append([0.0; 3]);                  // (resize does not exist on grow-only.)
    print(first + r + s[0] + g.len as f64);
}

fn push_through(v: f64[>..]&) { v.push(4.0); }

fn grow_via_ref() {
    var g: f64[>..] = [1.0];
    push_through(&g);
    print(g.len);
}

// A grow-only local may be cleared where no reference or slice into it can
// still be named (§5.1): the loop body's slice dies with its iteration, and
// the block's with the block.
fn clear_scratch() -> i64 {
    var scratch: u8[>..] = [];
    var total = 0;
    for i in 3 {
        scratch.clear();
        for j in i + 1 { scratch.push(('a' + j) as! u8); }
        let s = scratch[0..scratch.len];
        total += s.len + s[0];
    }
    block {
        let all = scratch[..];
        total += all.len;
    }
    scratch.clear();
    return total + scratch.len;
}

// `pop` and `resize` join `clear` on a grow-only local (§5.1): the slice
// lives in its block, and the shrinks stand between phases.
fn pop_scratch() -> i64 {
    var st: i64[>..] = [];
    for i in 5 { st.push(i); }
    var total = 0;
    block {
        let s = st[2..st.len];
        total += s.len;                  // 3
    }
    let last = st.pop();                 // 4
    total += last;
    st.resize(2);                        // 0 1
    st.resize(4, 9);                     // 0 1 9 9
    total += st.pop();                   // 9
    return total * 10 + st.len;          // 163
}

// --- reusable pools (§5.4) --------------------------------------------------

struct Slot { v: i64 = 0, next: i64 = -1 }

reusable var gpool: Slot[>..] = [];

fn use_pool(pool: Slot[>..]&) -> i64 {
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

// --- cycle return roots (§7.8) ----------------------------------------------
// A cycle's return roots are the fixpoint over its returns, computed before
// any body is checked, so a back edge into a function that has returned
// nothing yet still gets a real root: the parser below hands a parenthesised
// subexpression straight back through `pfactor` (no wrapper node), and the
// builder's base case sits after its recursive call.

enum PNode { PLit { v: i64 }, PSum { l: PNode..&<u16>, r: PNode..&<u16> } }

var ptext: u8[>..<] = [];
var ppos = 0;

recursive fn pexpr(pool: PNode..[>..]&) -> PNode..& {
    var lhs: PNode..& = pfactor(pool);
    loop {
        guard ppos < ptext.len && ptext[ppos] == '+';
        ppos++;
        let rhs: PNode..& = pfactor(pool);
        lhs .= pool.push(PNode.PSum { l: lhs, r: rhs });
    }
    return lhs;
}

recursive fn pfactor(pool: PNode..[>..]&) -> PNode..& {
    guard ppos < ptext.len else { return pool.push(PNode.PLit { v: 0 }); }
    let c = ptext[ppos];
    ppos++;
    if c == '(' {
        // The back edge into the cycle's entry, reached before any of its own
        // returns: the result is rooted at this activation's pool, so it needs
        // no wrapper node and its links stay relative.
        let inner: PNode..& = pexpr(pool);
        if ppos < ptext.len && ptext[ppos] == ')' { ppos++; }
        return inner;
    }
    return pool.push(PNode.PLit { v: (c - '0') as i64 });
}

recursive fn psum(n: PNode.PLit&) -> i64 { n.v }
recursive fn psum(n: PNode.PSum&) -> i64 { psum(n.l) + psum(n.r) }

// The base case is the *last* return, so the back edge above it sees no
// recorded root at all.
recursive fn pchain(pool: PNode..[>..]&, n: i64) -> PNode..& {
    if n > 0 {
        let rest: PNode..& = pchain(pool, n - 1);
        let leaf: PNode..& = pool.push(PNode.PLit { v: n });
        return pool.push(PNode.PSum { l: leaf, r: rest });
    }
    return pool.push(PNode.PLit { v: 0 });
}

fn cycleroots() {
    var pool: PNode..[>..] = [];
    ptext.clear();
    ptext.append("(1+(2+3))+4");
    ppos = 0;
    let root: PNode..& = pexpr(&pool);
    print(psum(root));                   // 10, with no node per paren.
    print(pool.len);                     // Seven: four literals, three sums.
    print(psum(pchain(&pool, 4)));       // 1+2+3+4.
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
    let q = Pair<Pair<i64>> { a: p, b: Pair<i64> { 3, 4 } };
    print(q.a.a + q.b.b);
    let s: Opt<i64> = Opt<i64>.Some { v: 5 };
    let n: Opt<i64> = Opt<i64>.None {};
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
recursive fn depth2<T>(x: T, n: i64) -> i64 {
    if n <= 0 { 0 } else { depth2(x, n - 1) + 1 }
}

// Forwarding a function value down through another HOF.
fn apply2<F>(x: i64) { F(x); }
fn forward2<F>(x: i64) { apply2(x, F); }

fn generic_fns() {
    let arr = [7, 8, 9];
    print(first(arr));                            // T inferred through slice.
    let f = [1.5, 2.5];
    print(first(f));
    print(same(1, 2));
    print(same("a", "b"));
    pick(1, 2.5);
    print(first<i64>([4, 5]));                    // Explicit type argument.
    print(outer2(5));
    print(outer2(1.5));
    print(depth2(true, 3));
    forward2(7) { print(it * 3) }
}

// --- function values and HOFs (§7.6) ----------------------------------------

fn repeat<F: fn(i64)>(n: i64) { for i in n { F(i); } }
fn fold<F>(xs: i64[:], var acc: i64) -> i64 {
    for x in xs { acc = F(acc, x); }
    return acc;
}

fn hofs() {
    repeat(3) { print(it) }
    let xs = [1, 2, 3];
    let sum = fold(xs, 0) { a, x => a + x };
    print(sum);
    fn double(v: i64) { print(v * 2) }
    repeat(2, double);
    var captured = 0;
    fn bump(v: i64) { captured += v; }
    bump(5);
    repeat(2) { captured += it; }
    print(captured);
}

// Plain return inside a function value exits the named function (§7.6).
fn find_gt(xs: i64[:], limit: i64) -> i64 {
    repeat(xs.len) {
        if xs[it] > limit { return xs[it]; }
    }
    return -1;
}

// --- multiple returns, nonfixed returns, NRVO shapes ------------------------

fn minmax(a: i64, b: i64) -> i64, i64 {
    if a < b { return a, b; }
    return b, a;
}

fn labelled(n: i64) -> u8[], i64 { return "num", n }

fn multi() {
    let lo, hi = minmax(4, 2);
    let name, v = labelled(7);
    print(lo + hi + v + name.len);
    let a, b = minmax(1, 2);
    print(find_gt([a, b], 1));
}

// --- case functions with references (§8.2) ----------------------------------

enum Node { Num { v: f64 }, Neg { of: Node..&<u16>? } }

var npool: Node..[>..] = [];

recursive fn eval(n: Node.Num&) -> f64 { n.v }
recursive fn eval(n: Node.Neg&) -> f64 {
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

enum State { Idle, Busy { job: i64 }, Done { code: i16 } }

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

fn intmatch(n: i64) {
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

fn guards_blocks(n: i64) -> i64 {
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

fn inner_fail(n: i64) -> i64 {
    if n < 0 { return -1 from process; }
    n * 2
}

fn middle(n: i64) -> i64 { inner_fail(n) + 1 }

fn process(n: i64) -> i64 {
    return middle(n);
}

// --- globals, writability into globals, struct-stored refs ------------------

var gcounter = 0;
let gname: u8[] = "goose";
var gvec = Float3 { 0.0, 0.0, 0.0 };

var gint = 5;
struct GHolder { r: i64& }
var gheld = GHolder { r: &gint };        // Global-rooted ref into a global.

fn globals() {
    gcounter += gname.len;
    gvec = gvec + Float3 { 1.0, 1.0, 1.0 };
    gheld.r .= &gcounter;                // Rebind: same-depth store, fine.
    gheld.r = 7;                         // Transparent: writes gcounter.
    print(gheld.r + gcounter);
}

// --- slices: bounds forms, re-slicing, writable slices ----------------------

fn fill(s: i64[:], v: i64) { for i in s.len { s[i] = v; } }

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

fn shadowing(n: i64) {
    var acc: i64;
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
    print(clear_scratch());
    print(pop_scratch());                // 163
    pools();
    build_tree();
    cycleroots();
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
