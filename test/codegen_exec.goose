// Codegen execution test: every print is hand-verified in
// expected/codegen_exec.out. Exercises representations and operations the
// other test files don't reach; not a meaningful program.

// --- arrays: length storages, varint lengths, limited dynamic ---------------

fn lenstores() {
    let a: i64[u8] = [10, 20, 30];
    print(a.len);                        // 3
    print(a[2]);                         // 30
    let b: u8[u16] = "wide";
    print(b.len);                        // 4
    let c: u8[varint] = "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789";
    print(c.len);                        // 130 (two-byte ULEB length)
    print(c[129]);                       // '9' = 57
    var n = 0;
    for x, i in c { if i < 3 { n += x; } }
    print(n);                            // '0'+'1'+'2' = 147
}

fn limited_dyn() {
    var l: i64[..] = [7, 8];             // Capacity = initial length (2).
    print(l.cap);                        // 2
    l.pop();
    l.push(9);
    print(l[0] + l[1]);                  // 16
    l = [1, 2];                          // Assign within capacity.
    print(l[0] + l[1]);                  // 3
    l.clear();
    print(l.len);                        // 0
    l.resize(2, 5);
    print(l[0] + l[1]);                  // 10
    var big: i64[..] = [..4];            // Empty, with construction-time capacity.
    print(big.cap);                      // 4
    big.push(6);
    big.append([1, 1, 1]);               // Fills to capacity.
    print(big.len + big[3]);             // 5
}

// --- append forms ------------------------------------------------------------

fn make_arr() -> i64[] { return [4, 5]; }
fn fwd_arr() -> i64[] { return make_arr(); }         // Element-run forwarding.
fn named_arr() -> i64[] {
    let a: i64[] = [7, 8];
    return a;                            // Named result: callee-side copy in run form.
}

fn appends() {
    var g: i64[>..] = [1];
    g.append([2, 3]);                    // Fixed array source.
    let s = [9, 9, 9];
    g.append(s[1..2]);                   // Slice source.
    g.append(make_arr());                // Call source (slides the header out).
    var total = 0;
    for x in g { total += x; }
    print(total);                        // 1+2+3+9+4+5 = 24
    print(g.len);                        // 6
    var sb: u8[>..] = "ab";
    sb.append("cd");
    sb.push('!');
    print(sb);                           // ab cd ! -> "abcd!"
    var vs: u8[][>..] = [];
    vs.push("one");
    vs.append(vs);                       // Append itself: sequential copy.
    var lens = 0;
    for w in vs { lens += w.len; }
    print(lens);                         // 6
    var g2: i64[>..] = [];
    g2.append(fwd_arr());                // Element-run through a forward.
    g2.append(named_arr());
    print(g2.len);                       // 4
    print(g2[0] + g2[3]);                // 4 + 8 = 12
}

// --- grow-shrink -------------------------------------------------------------

fn growshrink() {
    var gs: i64[>..<] = [1, 2, 3];
    print(gs.pop());                     // 3
    gs.push(7);
    gs.resize(1);
    print(gs.len);                       // 1
    gs.resize(4, 6);
    var t = 0;
    for x in gs { t += x; }
    print(t);                            // 1+6+6+6 = 19
    gs = [8];                            // Whole-resizable assignment.
    print(gs.len + gs[0]);               // 9
    gs.clear();
    print(gs.len);                       // 0
}

// A named result cleared while it is built at the caller's destination
// (§7.3): the reset goes to the value's own base, not the stack's.
fn built_twice(n: i64) -> u8[>..] {
    var out: u8[>..] = [];
    out.append("xyz");
    out.clear();
    out.append("ab");
    if n > 1 { out.push('!'); }
    return out;
}

// A grow-only local gives its element region back at clear (§5.1) and refills
// it from the base; slices taken after a clear read the new contents.
fn growclear() {
    var scratch: u8[>..] = [];
    var last = 0;
    for i in 3 {
        scratch.clear();
        scratch.append("ab");
        scratch.push(('0' + i) as! u8);
        let s = scratch[1..scratch.len];
        last = s.len * 100 + s[1] - '0';
    }
    print(last);                         // "b2": 2 * 100 + 2 = 202
    print(scratch.len);                  // 3
    // Variable-size elements: every refill re-lays out the same bytes.
    var words: u8[][>..] = [];
    var n = 0;
    for i in 2 {
        words.clear();
        words.push("alpha");
        words.push("beta");
        for w in words { n += w.len + i; }
    }
    print(n);                            // (5+4) + (5+1+4+1) = 20
    words.clear();
    print(words.len);                    // 0
    var acc: u8[>..] = [];
    acc.append("12");
    acc.append(built_twice(2));          // Cleared above what acc already holds.
    acc.append("34");
    print(acc);                          // "12ab!34"
}

// --- packed layouts, pads, equality -----------------------------------------

struct Padded { a: i8, pad, b: i16, pad 2, c: u8 = 3 }

fn pads() {
    let p = Padded { a: -1, b: 300 };
    let q = Padded { a: -1, b: 300, c: 3 };
    print(p == q);                       // true (pad bytes ignored)
    print(p.b - p.a + p.c);              // 300 + 1 + 3 = 304
    let r = Padded { a: -1, b: 301 };
    print(p == r);                       // false
}

struct VF { tag: varint, more: varint }

fn varint_fields() {
    let a = VF { tag: -64, more: 300 };
    let b = VF { tag: -64, more: 300 };
    print(a.tag + a.more);               // 236
    print(a == b);                       // true
    print(a == VF { tag: -64, more: 301 });  // false
}

// --- fixed-mode ADTs through references, equality ---------------------------

enum Mode { Off, Level { v: i16 }, Blink { on: u8, off: u8 } }

fn flip(m: Mode&) {
    let next: Mode = match m {
        Off     => Mode.Level { v: 5 },
        Level l => Mode.Blink { on: l.v as u8, off: 2 },
        Blink   => Mode.Off {},
    };
    m = next;                            // Replace the variant via the reference.
}

fn fixed_adts() {
    var m: Mode = Mode.Off {};
    flip(m);
    flip(m);
    print(match m { Blink b => b.on * 10 + b.off, _ => -1 });   // 52
    let m2: Mode = Mode.Blink { on: 5, off: 2 };
    print(m == m2);                      // true
    flip(m);
    print(match m { Off => 1, _ => 0 }); // 1
}

// --- relative references: u8 width, optional null ---------------------------

struct Link { v: u8, prev: Link&<u8>? }

fn rel_refs() {
    var chain: Link[>..] = [];
    let a: Link& = chain.push(Link { v: 1 });
    let b: Link& = chain.push(Link { v: 2, prev: a });
    chain.push(Link { v: 3, prev: b });
    var sum = 0;
    for &l in chain {
        sum = sum * 10 + l.v;
        if l.prev { sum += 100; }        // Non-null relative loads.
    }
    print(sum);                          // ((1)*10+2+100)*10+3+100 = 1223
    // Walk back from the last element.
    let last = &chain[2];
    var walk = 0;
    var cur: Link? = last;
    loop {
        guard cur;                       // Narrows (or exits the loop on null).
        walk = walk * 10 + cur.v;
        cur .= cur.prev;                 // Rebind through the loaded link.
    }
    print(walk);                         // 321
}

// --- relative references: self-linked construction ---------------------------

struct Ring { v: u8, prev: Ring&<u16>, next: Ring&<u16> }
enum Knot { Loop { n: u8, back: Knot.Loop&<u8> }, Plain { n: u8 } }

reusable var ringpool: Ring[>..] = [];

fn self_links() {
    // A sentinel: nothing else exists yet, so `self` is the only reference
    // its non-optional links can hold.
    let hi = ringpool.alloc_index(Ring { v: 1, prev: self, next: self });
    print(ringpool[hi].prev.v * 10 + ringpool[hi].next.v);   // 11
    // Two sentinels closed into a circle once both exist.
    let ti = ringpool.alloc_index(Ring { v: 2, prev: self, next: self });
    ringpool[hi].next .= ringpool[ti];
    ringpool[hi].prev .= ringpool[ti];
    ringpool[ti].next .= ringpool[hi];
    ringpool[ti].prev .= ringpool[hi];
    var cur: Ring& = ringpool[hi];
    var walk = 0;
    for i in 5 { walk = walk * 10 + cur.v; cur .= cur.next; }
    print(walk);                         // 12121
    var back = 0;
    for i in 3 { back = back * 10 + cur.v; cur .= cur.prev; }
    print(back);                         // 212
    // The same inside a variant literal, built at the stack top.
    var knots: Knot.Loop[>..] = [];
    let k: Knot.Loop& = knots.push(Knot.Loop { n: 4, back: self });
    print(k.back.back.n);                // 4
}

// --- relative references: literals built at their destination ---------------

struct RelCell { v: i64, link: RelCell&<u16>? }
struct RelPair { one: RelCell, two: RelCell }
struct RelSlot { n: i64 = 0, up: RelSlot&<u16>? }

fn relink(c: RelCell&, t: RelCell&) {
    c = RelCell { v: 9, link: t };       // Whole-value store through a reference.
}

// Every destination here is an element of the array the offsets range over,
// so the stored offsets are small; measured from anywhere else they overflow
// the u16 width and abort (§3.9).
fn rel_inplace() {
    var a: RelCell[>..] = [];
    a.push(RelCell { v: 1 });
    a.push(RelCell { v: 2 });
    a.push(RelCell { v: 3 });
    a[2] = RelCell { v: 30, link: a[1] };   // Element assignment.
    let l = a[2].link;
    if l { print(l.v * 10 + a[2].v); }   // 2*10+30 = 50
    relink(a[0], a[2]);
    let m = a[0].link;
    if m { print(m.v + a[0].v); }        // 30+9 = 39
    var p: RelPair[>..] = [];
    p.push(RelPair { one: RelCell { v: 1 }, two: RelCell { v: 2 } });
    p[0].two = RelCell { v: 7, link: p[0].one };   // Struct-typed field.
    let n = p[0].two.link;
    if n { print(n.v * 10 + p[0].two.v); }   // 1*10+7 = 17
}

fn rel_pool() {
    reusable var pool: RelSlot[>..] = [];
    let a = pool.alloc_index(RelSlot { n: 1 });
    let b = pool.alloc_index(RelSlot { n: 2, up: pool[a] });   // Built in the slot.
    let u = pool[b].up;
    if u { print(u.n * 10 + pool[b].n); }   // 12
    let c: RelSlot& = pool.alloc_ref(RelSlot { n: 3, up: pool[b] });
    let w = c.up;
    if w { print(w.n * 10 + c.n); }      // 23
}

// --- f32 storage and arrays --------------------------------------------------

struct F3 { x: f32, y: f32, z: f32 }

fn f32s() {
    var v = F3 { 1.5, 2.5, 3.5 };
    let w = F3 { 0.5, 0.5, 0.5 };
    let s = v + w;                       // Elementwise in f32.
    print(s.x + s.y + s.z);              // 2+3+4 = 9
    v.x *= 2.0;
    print(v.x);                          // 3
    let arr: f32[2] = [0.25, 0.75];
    print(arr[0] + arr[1]);              // 1
    print(v.y > v.x);                    // false (2.5 > 3 is false)
}

// --- control: break values, bytes-valued blocks ------------------------------

fn controls(n: i64) {
    let picked: u8[] = if n > 0 { "pos" } else { "neg" };
    print(picked);                       // pos
    var j = 0;
    let found = loop {
        if j * j > n { break j; }
        j++;
    };
    print(found);                        // 3 (9 > 8)
    let blocked = block {
        guard n != 8 else { break 5; }
        0
    };
    print(blocked);                      // 5
    var i = 0;
    loop {
        i++;
        guard i < 4;                     // Bare guard: valueless loop exit.
    }
    print(i);                            // 4
    void_like();
}

fn void_like() {
    for j in 2..5 {
        if j == 3 { continue; }
        print(j);                        // 2, 4
    }
}

// --- nested functions writing captured state (kept un-inlined at -O0) -------

fn capture_test() {
    var log: u8[>..] = [];
    var count = 0;
    fn note(c: i64) {
        log.push(c as! u8);
        count++;
    }
    note('x');
    note('y');
    print(log);                          // xy
    print(count);                        // 2
}

// --- multiple returns, mixed and forwarded ----------------------------------

fn three(n: i64) -> i64, u8[], bool {
    if n > 0 { return n, "up", true; }
    return -n, "down", false;
}

fn fwd(n: i64) -> i64, u8[], bool { return three(n); }

fn multis() {
    let a, s, f = fwd(-7);
    print(a);                            // 7
    print(s);                            // down
    print(f);                            // false
}

// --- long-distance returns with values (A.5 shape) ---------------------------

recursive fn deep(n: i64) -> i64 {
    if n == 0 { return 99, "boom" from top; }
    deep(n - 1) + 1
}

recursive fn deepr(n: i64) -> i64 {
    if n == 0 { return 0; }
    deepr(n - 1) + 1
}

fn top(n: i64) -> i64, u8[] {
    return deep(n) + 1000, "ok";
}

fn longdistance() {
    let v, msg = top(3);
    print(v);                            // 99
    print(msg);                          // boom
    print(deepr(5));                     // 5
}

// A propagating callee whose own result is a reference, so the discriminant
// cannot ride in the returned value.
struct RCell { v: i64 }

recursive fn refpick(pool: RCell[>..]&, n: i64) -> RCell& {
    if n == 0 { return -1 from refroot; }
    if n == 1 { return pool.push(RCell { v: 10 }); }
    return refpick(pool, n - 1);
}

fn refroot(n: i64) -> i64 {
    var pool: RCell[>..] = [];
    let c: RCell& = refpick(pool, n);
    return c.v + pool.len;
}

// Two intermediate frames with scalar results between the target and the site.
recursive fn scal_inner(n: i64) -> i64 {
    if n < 0 { return 7 from scal_top; }
    if n > 100 { return scal_inner(n - 100); }
    return n * 2;
}

recursive fn scal_mid(n: i64) -> i64 {
    if n > 1000 { return scal_mid(n - 1000); }
    return scal_inner(n) + 1;
}

fn scal_top(n: i64) -> i64 { scal_mid(n) + 100 }

fn longdistance2() {
    print(refroot(3));                   // 11: the pushed cell, plus one node
    print(refroot(0));                   // -1
    print(scal_top(4));                  // 109
    print(scal_top(0 - 1));              // 7
}

// --- NRVO both ways ----------------------------------------------------------

fn build(n: i64) -> u8[>..] {
    var out: u8[>..] = [];
    if n > 0 { out.push('a'); return out; }
    out.push('b');
    return out;                          // Same local: built at the destination.
}

fn pick_one(n: i64) -> u8[>..] {
    var x: u8[>..] = "x";
    var y: u8[>..] = "y";
    if n > 0 { return x; }               // Two sources: one copies on return.
    return y;
}

fn nrvo() {
    print(build(1));                     // a
    print(build(0));                     // b
    print(pick_one(1));                  // x
    print(pick_one(0));                  // y
}

// --- casts and unsigned arithmetic -------------------------------------------

fn casts() {
    print(300 as! u8);                   // 44
    print((0 - 1) as! u16);              // 65535 (`as` binds tighter than unary -)
    print(2.9 as! i64);                  // 2 (checked `as` would abort in debug)
    print(-2.9 as! i64);                 // -2
    print(7 as f64 / 2.0);               // 3.5
    print(1 << 65);                      // 2 (shift count masked to 1)
    print((0 - 1) as! u64 >> 60);        // 15
    print((0 - 2) as! u64 / 3);          // 6148914691236517204
    print((0 - 1) as! u64 % 10);         // 5
    print(1 as u64 < (0 - 1) as! u64);   // true
    print(hardware_threads() >= 1);      // true
}

// A u64 may be compared against a signed operand the compiler knows is
// non-negative (§6.1); the comparison is unsigned, so it stays correct for
// values above i64.max, where a hand-written `as!` cast would not be.
fn u64_compare() {
    var a: i64[>..] = [];
    for i in 5 { a.push(i); }
    let big = (0 - 1) as! u64;           // u64.max
    let small = 3 as u64;
    print(small < a.len);                // true
    print(big < a.len);                  // false (as! i64 would say true)
    print(big > a.len);                  // true
    print(big == a.len);                 // false
    print(small < 4);                    // true (literal)
    let n = a.len;                       // a `let` keeps the property
    print(small < n);                    // true
    print(big >= n);                     // true
    let cap: i64[..8] = [1, 2];
    print(small < cap.cap);              // true
    // The guarded index reduction the rule exists for: a u64 hash compared
    // against a length with no cast on the comparison itself.
    var sum = 0;
    var h: u64 = 7;
    for i in 4 {
        h = h * 3;                       // 21, 63, 189, 567
        let r = h % (a.len as u64);      // 1, 3, 4, 2
        if r < a.len { sum += a[r as! i64]; }
    }
    print(sum);                          // 10, since a[i] == i
}

// `%` is Euclidean at every integer type (§6.2): the result is in [0, |b|)
// whatever the dividend's sign, so it is always a valid index into `b` slots.
fn euclidean_mod() {
    var a = 7;
    var b = 3;
    print(a % b);                        // 1
    print((0 - a) % b);                  // 2  (C's truncating % gives -1)
    print(a % (0 - b));                  // 1
    print((0 - a) % (0 - b));            // 2
    var e: i32 = 0 - 7;
    print(e % 3);                        // 2 (narrow types agree)
    var m = -9223372036854775807 - 1;    // i64.min
    print(m % (0 - 1));                  // 0 (would trap as a hardware divide)
    print(m % 4);                        // 0
    print((m + 1) % 4);                  // 1
    var u: u32 = 7;
    print(u % 3);                        // 1 (unsigned is unchanged)
}

// --- match over variable payload by value ------------------------------------

enum Msg { Ping, Text { body: u8[] } }

fn msgs() {
    var q: Msg..[>..] = [];
    q.push(Msg.Text { body: "hi" });
    q.push(Msg.Ping);
    for m in q {
        match m {
            Ping   => print(0),
            Text t => print(t.body.len),   // By-value copy of the payload.
        }
    }                                    // 2, 0
}

// --- reusable pools: freelist reuse order ------------------------------------

struct PSlot { n: i64 = 0 }

fn pool_reuse() {
    reusable var pool: PSlot[>..] = [];
    let a = pool.alloc_index(PSlot { n: 1 });
    let b = pool.alloc_index(PSlot { n: 2 });
    pool.free(a);
    pool.free(b);
    let c = pool.alloc_index(PSlot { n: 3 });   // Reuses b's slot (LIFO).
    print(c == b);                       // true
    print(pool.len);                     // 2 (no fresh slots after reuse)
    print(a + c);                        // 0 + 1 = 1
    print(pool[0].n + pool[1].n);        // 1 + 3 = 4 (slot a still holds 1)
}

// --- globals initialized in order --------------------------------------------

let gbase = 40;
var gnext = gbase + 2;
var gtext: u8[>..] = "g";

var galias: u8[>..] = "";

struct RHold { p: u8[>..]& }

// The reference in the field may be the very global this body also pushes to,
// so neither spelling of that stack may hold its top anywhere but in memory.
fn hpush(h: RHold&, c: u8) -> i64 {
    galias.push('a');
    h.p.push(c);
    galias.push('z');
    return galias.len;
}

fn globals2() {
    print(gnext);                        // 42
    gtext.push('!');
    print(gtext);                        // g!
    let h = RHold { p: galias };
    print(hpush(h, 'm'));               // 3
    print(galias);                       // amz
}

// --- empty and zero-sized ----------------------------------------------------

fn empties() {
    let e: i64[] = [];
    print(e.len);                        // 0
    var g: f64[>..] = [];
    print(g.len);                        // 0
    let s: u8[:] = "";
    print(s.len);                        // 0
    print("");                           // (empty line)
}

// --- structs with resizable tails (§3.4) -------------------------------------

struct Tailed { count: i64, items: i64[>..] }

fn tails() {
    var t = Tailed { count: 1, items: [5] };
    t.items.push(6);
    t.count++;
    print(t.count + t.items.len + t.items[1]);   // 2+2+6 = 10
}

// --- sequential arrays copied across kinds -----------------------------------

fn seqvar() {
    var src: u8[][>..] = [];
    src.push("aa");
    src.push("bbb");
    let copied: u8[][] = copy(src);              // Variable array of variable elements.
    var n = 0;
    for w in copied { n += w.len; }
    print(n + copied.len);                 // 5 + 2 = 7
}

// --- sized locals, widening, per-width arithmetic (§6.1–§6.3) ----------------

fn addu8(a: u8, b: u8) -> u8 { a + b }

// Not folded at -O0, so the checked narrowing survives to the C: its range
// check names i32's bounds, i32.min among them.
fn narrow32(v: i64) -> i32 { v as i32 }

fn sized() {
    var a: u8 = 250;
    let b = a;                           // Inference keeps the size: b is u8.
    print(addu8(b, 5));                  // 255 (u8 math, at the u8 maximum)
    var w: i32 = -70000;
    let widened: i64 = w;                // Implicit widening, signed.
    print(widened - a);                  // -70250 (the u8 widens into the i64 op)
    var u: u32 = 4000000000;
    let big: i64 = u;                    // u32 -> i64 is value-preserving.
    print(big + 1);                      // 4000000001
    var h: u64 = 14695981039346656037;   // Above i64.max: prints unsigned.
    h = h ^ 33;
    print(h);                            // 14695981039346656004
    print(h % 10);                       // 4
    print(h / 3);                        // 4898660346448885334
    print(h > 1);                        // true (unsigned compare)
    h = h * 1099511628211;               // Unsigned math wraps in every build.
    print(h);                            // 12638116831811436492
    var total: i32 = 0;
    for i in 5 as i32 { total += i * i; }  // The loop variable runs at i32.
    print(total);                        // 30
    var narrow: i16 = -300;
    narrow--;
    print(narrow as i64 * 2);            // -602
    print((1 as u8) << 9);               // 2 (the count is masked to the width: 9 & 7)
    print(narrow32(widened));            // -70000 (checked narrowing back to i32)
    print(narrow32(-2147483648));        // -2147483648 (i32.min passes its own check)
}

// --- print edges -------------------------------------------------------------

fn printedges() {
    print(-9223372036854775807 - 1);     // int64 min
    print(-2147483648);                  // int32 min, as an i64 literal
    print(0.1);
    print(1.0e300);
    print(1.0 / 3.0);
}

// --- accumulator tail recursion ----------------------------------------------
// The optimizer rewrites a self-recursive fold into a loop from -O1 up, so the
// -O0 and -O2 runs of this file are what pin the rewrite to the recursion's
// value, evaluation order and float behaviour.

struct TreN { v: i32, l: TreN&<u32>?, r: TreN&<u32>? }

var trepool: TreN[>..] = [];

recursive fn trebuild(d: i64, v: i64) -> TreN& {
    if d == 0 { return trepool.push(TreN { v: v as! i32 }); }
    let l: TreN& = trebuild(d - 1, v * 2);
    let r: TreN& = trebuild(d - 1, v * 2 + 1);
    return trepool.push(TreN { v: v as! i32, l: l, r: r });
}

// Two self-calls: the left one stays a call, the right one becomes the loop.
recursive fn tresum(n: TreN&) -> i64 {
    let l = n.l;
    guard l else { return n.v; }
    let r = n.r;
    guard r else { return n.v; }
    return n.v + tresum(l) + tresum(r);
}

recursive fn treadd(n: i64) -> i64 {
    if n <= 0 { return 0; }
    return n + treadd(n - 1);
}

recursive fn tremul(n: i64) -> i64 {
    if n <= 1 { return 1; }
    return n * tremul(n - 1);
}

// A float fold must not be regrouped: 1.0 added to 1.0e16 is absorbed, added
// to itself it is not, so the two groupings give different answers.
recursive fn tredrift(n: i64) -> f64 {
    if n <= 0 { return 1.0e16; }
    return 1.0 + tredrift(n - 1);
}

// Evaluation order: the accumulated operand advances a global that the deeper
// calls read back, so swapping it with the call would change the sum.
// Unsigned arithmetic wraps in every build, so only the order is under test.
var trestate: u32 = 1;

fn trestep(k: u32) -> u32 {
    trestate = trestate * 2654435761 + k;
    return trestate;
}

recursive fn treorder(n: u32) -> u32 {
    if n == 0 { return trestate; }
    return trestep(n) + treorder(n - 1);
}

// Both parameters change at once and the new `b` reads the old `a`.
recursive fn treswap(a: i64, b: i64) -> i64 {
    if a <= 0 { return b; }
    return b + treswap(a - 1, a + b);
}

fn tailrec() {
    let root: TreN& = trebuild(3, 1);
    print(tresum(root));                 // 1 + 2 + ... + 15 = 120
    print(treadd(100));                  // 5050
    print(tremul(10));                   // 3628800
    print(tredrift(8));                  // 1e+16: every add is absorbed
    print(treorder(6));
    print(treswap(6, 1));                // 1 + 7 + 12 + 16 + 19 + 21 + 22 = 98
}

// --- values landing in layouts other than their own ------------------------

struct VCell { q: varint }
struct VItem { name: u8[varint], n: varint }
struct VBag { id: varint, items: VItem[varint] }
struct VLink { v: u8, kids: (VLink&<u16>)[..2] }
struct VSelf { v: i64, next: VSelf? }    // A fixed struct naming its own type.

fn vmod(a: u64) -> i64 { (a % 5) as! i64 }

// A resizable local returned as a variable array: the length prefix is
// written in front of its elements at the return.
fn vitems(k: i64) -> VItem[] {
    var t: VItem[>..] = [];
    for i in k { t.push(VItem { name: "ab", n: i + 1 }); }
    return t;
}

fn layouts() {
    var seed: u64 = 12;
    seed = seed * 7;
    let c = VCell { q: vmod(seed) };     // A call (inlined at -O2) as a varint initializer.
    print(c.q);                          // 4
    let plain: VItem[] = vitems(2);      // The callee's own length storage.
    var s = 0;
    for it in plain { s += it.n * 10 + it.name.len; }
    print(s);                            // 12 + 22 = 34
    let bag = VBag { id: 9, items: vitems(3) };   // A u32-length result in a varint-length slot.
    s = bag.id;
    for it in bag.items { s += it.n * 10 + it.name.len; }
    print(s);                            // 9 + 12 + 22 + 32 = 75
    var links: VLink[>..] = [];
    let root: VLink& = links.push(VLink { v: 1, kids: [] });
    let a: VLink& = links.push(VLink { v: 2, kids: [] });
    let b: VLink& = links.push(VLink { v: 3, kids: [] });
    root.kids.push(a);                   // References pushed into relative-reference slots.
    root.kids.push(b);
    print(root.kids[0].v * 10 + root.kids[1].v);   // 23
    var first = VSelf { v: 5 };
    var second = VSelf { v: 7, next: first };
    let nx = second.next;
    if nx { print(nx.v + second.v); }    // 12
}

// --- data-stack tops cached across the loops that grow them (C.4) ------------

// Inlined at -O2: the early return leaves the pushing loop by a jump, so the
// cached top has to reach memory on that path too.
fn evens_upto(n: i64, stop: i64) -> i64[>..] {
    var out: i64[>..] = [];
    for i in n {
        out.push(i * 2);
        if out.len == stop { return out; }
    }
    return out;
}

fn add_two(xs: i64[>..]&, v: i64) { xs.push(v); xs.push(v + 1); }

fn looptops() {
    var xs: i64[>..] = [];
    for i in 6 { xs.push(i); }           // Grows the stack: cached across here.
    var s = 0;
    for &x in xs { x = x * 3; s += x; }  // Elements only: the memory form.
    print(s);                            // 3 * 15 = 45
    for i in 3 { xs.push(100 + i); }     // A second region on the same stack.
    for i in 2 { add_two(xs, 200 + i * 2); }   // Grown by a callee instead.
    for &x in xs { s += x; }
    print(s + xs.len);                   // 45 + 1154 + 13 = 1212
    var ys: i64[>..] = [];
    for i in 10 { if i == 4 { break; } ys.push(i); }
    print(ys.len * 100 + ys[3]);         // 403
    let e = evens_upto(10, 4);
    var t = 0;
    for &v in e { t += v; }
    print(t * 10 + e.len);               // (0+2+4+6) * 10 + 4 = 124
}

// --- pushing through a reference, across calls that push through it too -----

struct RNode { tag: i64, l: RNode&<u32>?, r: RNode&<u32>? }

// A callee that pushes into the same array its caller is pushing into: the
// caller's stack top has to be in memory across the call and read back after.
fn rleaf(p: RNode[>..]&, v: i64) -> RNode& { return p.push(RNode { tag: v }); }

// The middle element is pushed between the two recursive calls and read again
// after the second, so a stale or unreloaded top shows up in the tags.
recursive fn rbuild(p: RNode[>..]&, d: i64) -> RNode& {
    if d == 0 { return rleaf(p, 1); }
    let a: RNode& = rbuild(p, d - 1);
    let m: RNode& = rleaf(p, d + 10);
    let b: RNode& = rbuild(p, d - 1);
    return p.push(RNode { tag: m.tag, l: a, r: b });
}

recursive fn rwalk(n: RNode&) -> i64, i64 {   // (tag digest, nodes on the tree)
    let l = n.l;
    guard l else { return n.tag, 1; }
    let r = n.r;
    guard r else { return n.tag, 1; }
    let ld, lc = rwalk(l);
    let rd, rc = rwalk(r);
    return n.tag + 3 * ld + 5 * rd, lc + rc + 1;
}

// A reusable pool by reference carries a second stack for the freelist; both
// are cached, and both cross the recursive call.
recursive fn pfill(p: PSlot[>..]&, n: i64) -> i64 {
    if n == 0 {
        let a = p.alloc_index(PSlot { n: 7 });
        p.free(a);
        return p.alloc_index(PSlot { n: 8 });   // Reuses a's slot.
    }
    let i = p.alloc_index(PSlot { n: n });
    return i + pfill(p, n - 1);
}

fn reftops() {
    var pool: RNode[>..] = [];
    let root: RNode& = rbuild(pool, 3);
    print(pool.len);                     // 22
    var tags = 0;
    for &n in pool { tags += n.tag; }
    print(tags);                         // 170
    let digest, nodes = rwalk(root);
    print(nodes);                        // 15 of the 22 are on the tree
    print(digest);                       // 1325
    reusable var slots: PSlot[>..] = [];
    print(pfill(slots, 3));             // 0 + 1 + 2 fresh, then 3 reused = 6
    print(slots.len);                    // 4
    var ns = 0;
    for &s in slots { ns += s.n; }
    print(ns);                           // 3 + 2 + 1 + 8 = 14
}

// --- read-back roots: an LRU list relinked through a slot array --------------
// The slots hold references, not indices: a reference read back out of a
// container whose only candidate pool is `lrupool` is rooted there exactly, so
// a node reached through the map can still be linked relatively (§9.5, §3.9).
// Only `free` still wants an index, which each node carries.

struct LruNode { key: i64, idx: i32, prev: LruNode&<u32>?, next: LruNode&<u32>? }
struct LruSlot { key: i64, node: LruNode&? }

reusable var lrupool: LruNode[>..] = [];
var lruslots: LruSlot[>..] = [];

fn lru_unlink(n: LruNode&) {
    let p = n.prev;
    let q = n.next;
    assert(p);
    assert(q);
    p.next .= q;
    q.prev .= p;
}

fn lru_front(head: LruNode&, n: LruNode&) {
    let q = head.next;
    assert(q);
    n.prev .= head;
    n.next .= q;
    q.prev .= n;
    head.next .= n;
}

// The head sentinel is lrupool[0], so both ends of the link are rooted at the
// global pool; a head *parameter* would be rooted at its own class instead.
fn lru_add(key: i64) {
    let i = lrupool.alloc_index(LruNode { key: key, idx: -1 });
    let n = &lrupool[i];
    n.idx = i as! i32;
    lru_front(lrupool[0], n);
    lruslots.push(LruSlot { key: key, node: n });
}

// The list from the head, most recent first.
fn lru_order() -> i64 {
    var order = 0;
    var cur = lrupool[0].next;
    loop {
        guard cur;
        guard cur.key >= 0;
        order = order * 10 + cur.key;
        cur .= cur.next;
    }
    order
}

fn lru_slots() {
    // Two sentinels, so no link is ever null.
    let hi = lrupool.alloc_index(LruNode { key: -1, idx: 0 });
    let ti = lrupool.alloc_index(LruNode { key: -1, idx: 1 });
    lrupool[hi].next .= lrupool[ti];
    lrupool[ti].prev .= lrupool[hi];
    for k in 4 { lru_add(k + 1); }
    print(lrupool.len);                  // 6: two sentinels and four nodes
    print(lru_order());                  // 4321
    // Touch key 2 through the slot array: the read-back relinks it to the
    // front, which is what the index-keyed version needed the index for.
    let m = lruslots[1].node;
    assert(m);
    lru_unlink(m);
    lru_front(lrupool[hi], m);
    print(lru_order());                  // 2431
    // Evict it; the freed slot comes back on the next alloc, still a valid
    // LruNode (§5.4).
    lru_unlink(m);
    lrupool.free(m.idx);
    lru_add(9);
    print(lru_order());                  // 9431
    print(lrupool.len);                  // 6: the freed slot was reused
}

// --- relative references measured from a named pool (§3.9) ------------------

// A varint link: the pool's elements are variable-size, so it is walked
// rather than indexed, and the offset is the unsigned LEB position in it.
struct VarNode { v: i64, up: VarNode&<varint in vpool>? }
var vpool: VarNode[>..] = [];

// A u64 link never needs a range check: no reservation reaches 2^64 bytes.
struct WNode { v: i64, link: WNode&<u64 in wpool>? }
struct WSlot { key: i64, node: WNode&<u64 in wpool>? }
var wpool: WNode[>..] = [];

fn inpool_varint() {
    let root: VarNode& = vpool.push(VarNode { v: 7 });
    let kid: VarNode& = vpool.push(VarNode { v: 8, up: root });
    let u = kid.up;
    assert(u);
    print(u.v);                          // 7
    var walk = 0;
    for n in vpool { walk = walk * 10 + n.v; }
    print(walk);                         // 78
}

fn inpool_wide() {
    wpool.push(WNode { v: 5 });
    wpool.push(WNode { v: 6 });
    wpool[1].link .= wpool[0];
    var st: WSlot[..4] = [];
    st.push(WSlot { key: 1, node: wpool[1] });
    // A value whose relative fields all name a pool copies: `pop` returns one.
    let top = st.pop();
    let n = top.node;
    assert(n);
    print(n.v);                          // 6
    let up = n.link;
    assert(up);
    print(up.v);                         // 5
    print(wpool.index_of(n));            // 1
}

// --- index_of on plain arrays (§3.3) -----------------------------------------
// Not a pool feature: any fixed-stride array converts a reference to one of
// its own elements back into the index.

struct FSlot { key: i64 }

fn index_of_kinds() {
    let f: FSlot[3] = [FSlot { key: 1 }, FSlot { key: 2 }, FSlot { key: 3 }];
    print(f.index_of(f[2]));            // 2
    var l: FSlot[..4] = [FSlot { key: 9 }];
    l.push(FSlot { key: 8 });
    print(l.index_of(l[1]));            // 1
}

// --- array views through references ------------------------------------------

fn stencil(src: i64[>..]&, dst: i64[>..]&) {
    // Element stores change neither length, so both views are read once here;
    // the growth after the loop still goes through the reference's own header.
    var i = 1;
    while i < src.len - 1 {
        dst[i] = src[i - 1] + src[i] * 2 + src[i + 1];
        i += 1;
    }
    src.push(src.len);
}

fn tailgrow(a: i64[>..]&, n: i64) {
    // The push in the body changes the length, so `a.len` is re-read: a view
    // hoisted here would keep reading the element it started at.
    for k in n { a.push(a[a.len - 1] + 1); }
}

fn loopviews() {
    var s: i64[>..] = [1, 2, 3, 4];
    var d: i64[>..] = [0, 0, 0, 0];
    stencil(s, d);
    var t = 0;
    for x in d { t += x; }
    print(t);                            // (1+4+3) + (2+6+4) = 20
    print(s.len * 10 + s[4]);            // 54: the push landed after [1, 2, 3, 4]
    tailgrow(s, 3);
    t = 0;
    for x in s { t += x; }
    print(t);                            // 1+2+3+4+4 + 5+6+7 = 32
}

// --- named results of inlined bodies -----------------------------------------
// Each builder below is used once, so from -O1 the optimizer splices it into
// its call site; its named result is then built at the destination the value
// lands in (§7.3), with the receiving length prefix reserved in front of the
// elements and patched at the return. At -O0 the same functions take the
// non-inlined path, so the two levels agreeing is what this tests.

struct NRec  { tag: u8, s: u8[varint], n: varint }
struct NLong { s: u8[varint], k: varint }
struct NWide { s: u8[], n: i64 }
struct NPair { a: u8[varint], b: u8[varint] }

fn nr_short() -> u8[] {
    var s: u8[>..] = [];
    for i in 5 { s.push(('a' + i) as! u8); }
    return s;
}

// 200 elements: the count outgrows the single byte a varint prefix reserves,
// so the elements move up by one when it is patched.
fn nr_long() -> u8[] {
    var s: u8[>..] = [];
    for i in 200 { s.push(('a' + i % 26) as! u8); }
    return s;
}

fn nr_wide() -> u8[] {
    var s: u8[>..] = [];
    for i in 4 { s.push(('m' + i) as! u8); }
    return s;
}

fn nr_pushed() -> u8[] {
    var s: u8[>..] = [];
    for i in 3 { s.push(('x' + i) as! u8); }
    return s;
}

fn nr_inner() -> u8[] {
    var s: u8[>..] = [];
    for i in 4 { s.push(('p' + i) as! u8); }
    return s;
}

// A builder whose elements are themselves built by one: both named results
// land on the same destination stack, nested.
fn nr_pairs(k: i64) -> NPair[] {
    var t: NPair[>..] = [];
    for i in k { t.push(NPair { a: nr_inner(), b: "z" }); }
    return t;
}

fn nr_grown() -> u8[] {
    var s: u8[>..] = [];
    for i in 6 { s.push(('A' + i) as! u8); }
    return s;
}

fn nr_two() -> u8[>..], i64 {
    var s: u8[>..] = [];
    s.push('m');
    s.push('n');
    return s, 5;
}

// A multi-name receive is not a named result: those locals take the call's
// channels on a stack of their own, so this one is copied on return.
fn nr_recv() -> u8[] {
    let a, b = nr_two();
    return a;
}

fn inlined_results() {
    let rec = NRec { tag: 1, s: nr_short(), n: 7 };
    print(rec.tag);                      // 1
    print(rec.s);                        // abcde
    print(rec.n);                        // 7
    let big = NLong { s: nr_long(), k: 42 };
    print(big.s.len);                    // 200
    print(big.s[0]);                     // 97
    print(big.s[199]);                   // 114
    print(big.k);                        // 42
    let w = NWide { s: nr_wide(), n: 3 };   // A u32 prefix: reserved, never moved.
    print(w.s);                          // mnop
    print(w.n);                          // 3
    var nlist: u8[varint][>..] = [];
    nlist.push(nr_pushed());             // A push destination.
    for e in nlist { print(e); }        // xyz
    let ps = nr_pairs(2);
    var acc = 0;
    for p in ps { acc += p.a.len * 10 + p.b.len; }
    print(acc);                          // 82
    var g: u8[>..] = nr_grown();         // A resizable receiver: only the count.
    print(g);                            // ABCDEF
    let rv = NLong { s: nr_recv(), k: 8 };
    print(rv.s);                         // mn
    print(rv.k);                         // 8
}

// Length prefixes on both sides of the one-byte ULEB boundary: 131 variable
// elements (a two-byte prefix on the array itself), 130 of them with a
// one-byte inline length and one with a two-byte one, so a walk over this
// value steps across both encodings.
fn vlong() -> VItem[] {
    var t: VItem[>..] = [];
    for i in 130 { t.push(VItem { name: "ab", n: 1 }); }
    t.push(VItem { name: "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789", n: 2 });
    return t;
}

fn lenprefixes() {
    let bag = VBag { id: 0, items: vlong() };
    var chars = 0;
    var ns = 0;
    for it in bag.items { chars += it.name.len; ns += it.n; }
    print(bag.items.len);                // 131
    print(chars);                        // 130 * 2 + 130 = 390
    print(ns);                           // 130 * 1 + 2 = 132
}

// --- self-recursive base cases ----------------------------------------------

// Each of these starts with a base case over its parameters alone, which the
// optimizer splices into every self-call site (optimize_basecase.h): the tree
// built and the walks over it must come out the same either way.

struct BNode { v: i64, l: BNode&<u32>?, r: BNode&<u32>? }

var rmarks = 0;

recursive fn bbuild(pool: BNode[>..]&, depth: i64, v: i64) -> BNode& {
    if depth == 0 { return pool.push(BNode { v: v }); }
    let l: BNode& = bbuild(pool, depth - 1, v * 2);
    let r: BNode& = bbuild(pool, depth - 1, v * 2 + 1);
    return pool.push(BNode { v: v, l: l, r: r });
}

recursive fn rsum(n: BNode&, d: i64) -> i64 {          // Guard form: arms swap.
    guard d > 0 else { return n.v; }
    var t = n.v;
    let l = n.l;
    if l { t += rsum(l, d - 1); }
    let r = n.r;
    if r { t += rsum(r, d - 1); }
    t
}

recursive fn rmark(n: BNode&, d: i64) {                // Valueless base case.
    if d == 0 { return; }
    rmarks += 1;
    let l = n.l;
    if l { rmark(l, d - 1); }
    let r = n.r;
    if r { rmark(r, d - 1); }
}

fn basecases() {
    var pool: BNode[>..] = [];
    let root: BNode& = bbuild(pool, 3, 1);
    print(pool.len);                     // A complete tree of depth 3: 15
    print(rsum(root, 3));                // Values 1..15: 120
    rmark(root, 3);
    print(rmarks);                       // Everything above the leaves: 7
}

// --- pop and resize on a grow-only local (§5.1) -------------------------------

// The stack is popped and resized where nothing points into it, and sliced
// in between; the element region it gives back is reused by its own pushes.
fn growshrink_local() {
    var st: i64[>..] = [];
    for i in 5 { st.push(i * i); }       // 0 1 4 9 16
    var acc = 0;
    acc += st.pop();                     // 16
    acc += st.pop();                     // 9
    block {
        let s = st[1..st.len];           // what remains: 1 4
        acc += s[0] * 100 + s[1] * 10;   // 140
    }
    print(acc);                          // 16 + 9 + 140 = 165
    st.resize(1);                        // 0
    st.resize(3, 7);                     // 0 7 7
    let a = st.pop();
    let b = st.pop();
    st.push(a + b);                      // 0 14
    print(st.len * 100 + st[1]);         // 214
    var top: i64[>..] = [];
    top.push(st.len);
    print(top[0] + st[0]);               // 2
}

// --- untyped parameters bind a reference argument as a reference (§3.8) ----

fn bump_any(x) { x += 1; }               // x: i64& when called with &a.
fn first_any(xs) -> i64 { xs[0] }        // xs: i64[>..<]& when called with &q.

fn untyped_refs() {
    var a = 41;
    bump_any(&a);
    print(a);                            // 42
    var q: i64[>..<] = [7, 8];
    print(first_any(q));                // 7
    q.push(9);
    print(first_any(q) + q.len);        // 10
}

// --- default<T>(): zero values with declared field defaults applied (§4.2) --

struct DefA { n: i64 = 7, f: f32, o: i64?, s: u8[:], lim: u8[..4], arr: i32[2] }
struct DefB { a: DefA, k: u8 = 3 }
enum DefE { Off { v: i16 = -2 }, On { x: i64 } }

fn defaults_builtin() {
    let d = default<DefB>();
    print(d.a.n + d.k);                  // 10
    print(d.a.f);                        // 0
    print(d.a.o == null);                // true
    print(d.a.s.len + d.a.lim.len + d.a.arr.len);   // 0 + 0 + 2 = 2
    print(d.a.arr[1]);                   // 0
    let e = default<DefE>();
    print(match e { Off o => o.v, On x => x.x });   // -2 (variant 0, its default)
    print(default<i64>() + default<u8>());          // 0
    print(default<bool>());              // false
    let arr = default<DefA[2]>();
    print(arr[1].n);                     // 7
}

// --- print / str / format: text forms (§3.7) ---------------------------------

fn text_builtins() {
    print("x = ", 42, ", y = ", 2.5, " ", true);      // x = 42, y = 2.5 true
    print();                                           // (empty line)
    let s = str("n", 7, "!");
    print(s);                                          // n7!
    print(s.len);                                      // 3
    var out: u8[>..] = [];
    format(out, "a", 1);
    out.format(-2, "b", 3.5);
    print(out);                                        // a1-2b3.5
    var words: u8[][>..] = [];
    words.push(str("w", 1));                           // A u8[] element: length prefix.
    words.push(str(2));
    for w in words { print(w); }                      // w1, 2
    var lim: u8[..8] = [];
    format(&lim, 12, "ab");                            // Capacity-checked.
    print(lim.len);                                    // 4
    print(lim);                                        // 12ab
    var acc: u8[>..] = "[";
    acc.append(str(9, 9));                             // Contiguous with acc's elements.
    print(acc);                                        // [99
    let u: u64 = 18446744073709551615;
    print(u, " ", 1.0e300, " ", 0.1);                  // 18446744073709551615 1e+300 0.1
    print(str(0.1), str(), str(false));                // 0.1false
}

// --- `var x = []`: the element type from the first push/append/assignment --

fn doubled_pa<T, F>(xs: T[:]) {
    var out = [];                        // Completed by the push below.
    for x in xs { out.push(F(x)); }
    return out;
}

fn twice_pa(x: i64) -> i64 { x * 2 }

fn pending_arrays() {
    var ns = [];
    ns.push(3);
    ns.push(4);
    print(ns.len + ns[1]);               // 6
    ns.push(twice_pa(5));                // A call result constructed at the top.
    print(ns[2]);                        // 10
    var words = [];
    words.push("ab");                    // A string literal makes it u8[][>..].
    words.push("cde");
    var n = 0;
    for w in words { n += w.len; }
    print(n);                            // 5
    let ys = doubled_pa([1, 2, 3]) { it * 2 };
    print(ys[2]);                        // 6
    var zs = [];
    zs = copy(ys);                             // Completed by the assignment.
    print(zs.len);                       // 3
    var txt = [];
    format(txt, "n=", 5);               // Completed by format: u8[>..].
    print(txt);                          // n=5
    var fl = [];
    fl.append([1.5, 2.5]);               // Completed by append: f64[>..].
    print(fl[1]);                        // 2.5
    print(count_pa([]));                 // 0: a `[]` literal argument adapts as before.
}

fn count_pa(xs: i64[]) -> i64 { xs.len }

fn head2_gs(q: i64[>..<]&) -> i64[:] { q[0..2] }
fn drop_last_gs(q: i64[>..<]&) { q.pop(); }
fn sum_gs(xs: i64[:]) -> i64 {
    var n = 0;
    for x in xs { n += x; }
    return n;
}

fn growshrink_slices() {
    var q: i64[>..<] = [1, 2, 3, 4];
    block { let s = q[1..3]; print(s[0] + s[1]); }   // 5
    block { let r = &q[3]; r = 40; }
    q.pop();                                   // Nothing in scope refers into q.
    print(q.len);                              // 3
    for &x in q { x += 1; }                    // 2, 3, 4
    print(q[0] + q[1] + q[2]);                 // 9
    block { let r2: i64& = q.push(7); r2 = 8; }   // push returns a reference.
    print(q[3]);                               // 8
    print(q.index_of(q[2]));                  // 2
    print(sum_gs(q[..]));                      // 17: a slice argument.
    block { let h = head2_gs(q); print(h.len); }    // 2: a returned slice.
    drop_last_gs(q);                          // A shrink through a reference.
    print(q.len);                              // 3
}

fn grow_pr(b: BagPr&) { b.items.push(7); }
fn total_pr(xs: i64[:]) -> i64 { var n = 0; for x in xs { n += x; } return n; }
fn untyped_len_pr(xs) -> i64 { xs.len }
fn inc_pr(n: i64&) { n += 1; }
fn make_pr() -> u8[] { "made" }
struct BagPr { items: i64[>..] }

fn passing_rules() {
    var b = BagPr { items: [1, 2, 3] };
    b.grow_pr();                     // UFCS: a non-fixed receiver binds by reference.
    print(total_pr(b.items));        // 13: an array to a slice parameter.
    var words: u8[][>..] = ["ab", "cde"];
    print(untyped_len_pr(words));    // 2: an untyped parameter binds by reference.
    for w in words { print(w.len); } // 2, 3: non-fixed elements bind by reference.
    var n = 1;
    inc_pr(n);                       // A fixed lvalue binds to a reference parameter.
    print(n);                        // 2
    let r: i64& = n;                 // An annotated reference needs no &.
    r = 5;
    print(n);                        // 5
    var items2: i64[>..] = copy(b.items);
    items2.push(9);
    print(items2.len, b.items.len);  // 54
    var out: u8[][>..] = [];
    for w in words { out.push(copy(w)); }
    out.push(make_pr());             // An rvalue constructs in place.
    print(out.len);                  // 3
    let s = make_pr();
    print(s);                        // made
}

struct BagFo { n: i64, items: i64[>..] }
struct NestFo { tag: i64, bag: BagFo }

fn grow_fo(xs: i64[>..]&) { xs.push(9); }
fn count_fo(xs: i64[:]) -> i64 { xs.len }
fn make_bag_fo(k: i64) -> BagFo {
    var b = BagFo { n: k, items: [] };
    for i in k { b.items.push(i); }
    return b;
}
fn total_fo(b: BagFo&) -> i64 {
    var s = b.n;
    for x in b.items { s += x; }
    return s;
}
fn take_fo(b: BagFo) -> i64 { b.items.len + b.n }

var gbag_fo: BagFo = BagFo { n: 1, items: [5] };

// A resizable-tailed struct is a frame object (C.2): its tail has a header
// of its own, so the tail can be referenced, sliced, and grown through a
// reference like a standalone resizable.
fn frame_objects() {
    var bag = BagFo { n: 10, items: [1, 2, 3] };
    bag.items.push(4);
    print(bag.items.len);                 // 4
    grow_fo(bag.items);                   // A reference to the tail.
    print(bag.items[4]);                  // 9
    print(count_fo(bag.items));           // 5: a slice of the tail.
    print(total_fo(bag));                 // 29
    let b2 = copy(bag);
    bag.items.push(100);
    print(b2.items.len, bag.items.len);   // 56
    var mb = make_bag_fo(3);
    print(total_fo(mb));                  // 6
    print(take_fo(make_bag_fo(2)));       // 4
    bag = make_bag_fo(1);
    print(bag.items.len, bag.n);          // 11
    var nest = NestFo { tag: 7, bag: BagFo { n: 2, items: [8] } };
    nest.bag.items.push(9);
    print(nest.tag + nest.bag.n + nest.bag.items[1]);   // 18
    grow_fo(nest.bag.items);
    print(nest.bag.items.len);            // 3
    let r: BagFo& = nest.bag;
    print(r.items[0]);                    // 8
    gbag_fo.items.push(6);
    print(gbag_fo.items.len + gbag_fo.n); // 3
}

fn doubled_bf(xs: i64[:]) -> i64[>..] {
    var out: i64[>..] = [];
    for x in xs { out.push(x * 2); }
    return out;
}
fn sum_bf<T>(xs: T[:]) -> T { var s: T = 0; for x in xs { s += x; } return s; }
struct unit_bf {}
fn find_bf<T, F>(xs: T[:]) -> T? {
    for &x in xs { if F(x) { return x; } }
    return null;
}

fn bug_fixes() {
    let a = [1, 2, 3];
    var vs: i64[][>..] = [];
    vs.push(doubled_bf(a));              // A resizable result in a variable-array slot.
    for v in vs { print(v.len, v[2]); }  // 36
    var g: i64[>..] = [1, 2, 3];
    let r = g;                           // A reference: g is not fixed-size.
    print(sum_bf(r));                    // 6: a reference to an array as a slice.
    let sl: u8[:] = "hello";
    var o: u8[>..] = sl;                 // A resizable built from a slice value.
    o.push(33);
    print(o);                            // hello!
    var us: unit_bf[>..] = [];
    us.push(unit_bf {});
    us.push(unit_bf {});
    print(us.len);                       // 2
    let f1 = find_bf(a) { it > 1 };
    if f1 { print(f1); }                 // 2
    let f2 = find_bf(a) { it > 9 };
    print(f2 == null);                   // true
}

extern fn sqrt(x: f64) -> f64;
extern "fabs" fn abs_ex(x: f64) -> f64;
extern "gs_os_getenv" fn getenv_ex(name: u8[:], out: u8[>..]&) -> bool;

// extern fns (§7.10): libm through the emitted prototypes, and a
// runtime-defined one taking a slice and a builder reference.
fn externs() {
    print(sqrt(16.0) == 4.0, abs_ex(-2.5) == 2.5);   // truetrue
    var v: u8[>..] = [];
    print(getenv_ex("GOOSE_NO_SUCH_VARIABLE_", v), v.len);   // false0
}

struct PtAg { x: i64, y: f64 }
struct NamedAg { name: u8[], p: PtAg, tags: i64[3] }
enum ShapeAg { Dot, Circle { r: f64 }, Rect { w: i64, h: i64 } }
struct MoneyAg { cents: i64 }
fn format(out: u8[>..]&, m: MoneyAg) { format(out, "$", m.cents / 100, ".", m.cents % 100); }
struct WalletAg { owner: u8[], cash: MoneyAg }

// Aggregates render as their positional literal, strings quoted inside
// them, references as their pointee, and a type with a user format
// overload through it wherever it occurs (§3.7).
fn aggregate_print() {
    print(PtAg { 1, 2.5 });                                   // PtAg { 1, 2.5 }
    let n = NamedAg { name: "bob", p: PtAg { 3, 4.0 }, tags: [7, 8, 9] };
    print(n);                                                 // NamedAg { "bob", PtAg { 3, 4 }, [7, 8, 9] }
    var pts: PtAg[>..] = [PtAg { 1, 1.0 }, PtAg { 2, 2.0 }];
    print(pts);                                               // [PtAg { 1, 1 }, PtAg { 2, 2 }]
    var words: u8[][>..] = ["a b", "c"];
    print(words);                                             // ["a b", "c"]
    var o: PtAg? = null;
    print(o);                                                 // null
    o .= n.p;
    print(o);                                                 // PtAg { 3, 4 }
    var sh: ShapeAg[>..] = [ShapeAg.Dot, ShapeAg.Circle { r: 1.5 }, ShapeAg.Rect { w: 2, h: 3 }];
    print(sh);                                                // [ShapeAg.Dot, Circle { 1.5 }, Rect { 2, 3 }]
    print(MoneyAg { 1234 });                                  // $12.34
    print(WalletAg { owner: "ann", cash: MoneyAg { 250 } });  // WalletAg { "ann", $2.50 }
    let s = str("v=", PtAg { 5, 6.0 });
    print(s.len, " ", s);                                     // 15 v=PtAg { 5, 6 }
    var out: u8[>..] = [];
    format(out, "[", MoneyAg { 5 }, "]");
    print(out);                                               // [$0.5]
}

fn main_codegen_exec() {
    lenstores();
    limited_dyn();
    appends();
    growshrink();
    growclear();
    pads();
    varint_fields();
    fixed_adts();
    rel_refs();
    self_links();
    rel_inplace();
    rel_pool();
    f32s();
    controls(8);
    capture_test();
    multis();
    longdistance();
    longdistance2();
    nrvo();
    casts();
    u64_compare();
    euclidean_mod();
    msgs();
    pool_reuse();
    globals2();
    empties();
    tails();
    seqvar();
    sized();
    printedges();
    tailrec();
    layouts();
    looptops();
    reftops();
    lru_slots();
    inpool_varint();
    inpool_wide();
    index_of_kinds();
    loopviews();
    inlined_results();
    lenprefixes();
    basecases();
    growshrink_local();
    untyped_refs();
    defaults_builtin();
    text_builtins();
    pending_arrays();
    growshrink_slices();
    passing_rules();
    frame_objects();
    bug_fixes();
    externs();
    aggregate_print();
}

// Standalone entry; ignored when this file is imported (§11.1).
fn main() { main_codegen_exec(); }
