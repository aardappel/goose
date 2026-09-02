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
    for &w in vs { lens += w.len; }
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
    flip(&m);
    flip(&m);
    print(match m { Blink b => b.on * 10 + b.off, _ => -1 });   // 52
    let m2: Mode = Mode.Blink { on: 5, off: 2 };
    print(m == m2);                      // true
    flip(&m);
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
    let c: RCell& = refpick(&pool, n);
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
    for &m in q {
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
    let h = RHold { p: &galias };
    print(hpush(&h, 'm'));               // 3
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
    let copy: u8[][] = src;              // Variable array of variable elements.
    var n = 0;
    for &w in copy { n += w.len; }
    print(n + copy.len);                 // 5 + 2 = 7
}

// --- sized locals, widening, per-width arithmetic (§6.1–§6.3) ----------------

fn addu8(a: u8, b: u8) -> u8 { a + b }

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
}

// --- print edges -------------------------------------------------------------

fn printedges() {
    print(-9223372036854775807 - 1);     // int64 min
    print(0.1);
    print(1.0e300);
    print(1.0 / 3.0);
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
    for &it in plain { s += it.n * 10 + it.name.len; }
    print(s);                            // 12 + 22 = 34
    let bag = VBag { id: 9, items: vitems(3) };   // A u32-length result in a varint-length slot.
    s = bag.id;
    for &it in bag.items { s += it.n * 10 + it.name.len; }
    print(s);                            // 9 + 12 + 22 + 32 = 75
    var links: VLink[>..] = [];
    let root: VLink& = links.push(VLink { v: 1, kids: [] });
    let a: VLink& = links.push(VLink { v: 2, kids: [] });
    let b: VLink& = links.push(VLink { v: 3, kids: [] });
    root.kids.push(a);                   // References pushed into relative-reference slots.
    root.kids.push(b);
    print(root.kids[0].v * 10 + root.kids[1].v);   // 23
    var first = VSelf { v: 5 };
    var second = VSelf { v: 7, next: &first };
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
    for i in 2 { add_two(&xs, 200 + i * 2); }   // Grown by a callee instead.
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
    let root: RNode& = rbuild(&pool, 3);
    print(pool.len);                     // 22
    var tags = 0;
    for &n in pool { tags += n.tag; }
    print(tags);                         // 170
    let digest, nodes = rwalk(root);
    print(nodes);                        // 15 of the 22 are on the tree
    print(digest);                       // 1325
    reusable var slots: PSlot[>..] = [];
    print(pfill(&slots, 3));             // 0 + 1 + 2 fresh, then 3 reused = 6
    print(slots.len);                    // 4
    var ns = 0;
    for &s in slots { ns += s.n; }
    print(ns);                           // 3 + 2 + 1 + 8 = 14
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
    stencil(&s, &d);
    var t = 0;
    for x in d { t += x; }
    print(t);                            // (1+4+3) + (2+6+4) = 20
    print(s.len * 10 + s[4]);            // 54: the push landed after [1, 2, 3, 4]
    tailgrow(&s, 3);
    t = 0;
    for x in s { t += x; }
    print(t);                            // 1+2+3+4+4 + 5+6+7 = 32
}

fn main_codegen_exec() {
    lenstores();
    limited_dyn();
    appends();
    growshrink();
    pads();
    varint_fields();
    fixed_adts();
    rel_refs();
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
    layouts();
    looptops();
    reftops();
    loopviews();
}

// Standalone entry; ignored when this file is imported (§11.1).
fn main() { main_codegen_exec(); }
