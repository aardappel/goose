// Codegen execution test: every print is hand-verified in
// expected/codegen_exec.out. Exercises representations and operations the
// other test files don't reach; not a meaningful program.

// --- arrays: length storages, varint lengths, limited dynamic ---------------

fn lenstores() {
    let a: int[u8] = [10, 20, 30];
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
    var l: int[..] = [7, 8];             // Capacity = initial length (2).
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
}

// --- append forms ------------------------------------------------------------

fn make_arr() -> int[] { return [4, 5]; }

fn appends() {
    var g: int[>..] = [1];
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
}

// --- grow-shrink -------------------------------------------------------------

fn growshrink() {
    var gs: int[>..<] = [1, 2, 3];
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

fn controls(n: int) {
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
    fn note(c: int) {
        log.push(c as! u8);
        count++;
    }
    note('x');
    note('y');
    print(log);                          // xy
    print(count);                        // 2
}

// --- multiple returns, mixed and forwarded ----------------------------------

fn three(n: int) -> int, u8[], bool {
    if n > 0 { return n, "up", true; }
    return -n, "down", false;
}

fn fwd(n: int) -> int, u8[], bool { return three(n); }

fn multis() {
    let a, s, f = fwd(-7);
    print(a);                            // 7
    print(s);                            // down
    print(f);                            // false
}

// --- long-distance returns with values (A.5 shape) ---------------------------

recursive fn deep(n: int) -> int {
    if n == 0 { return 99, "boom" from top; }
    deep(n - 1) + 1
}

recursive fn deepr(n: int) -> int {
    if n == 0 { return 0; }
    deepr(n - 1) + 1
}

fn top(n: int) -> int, u8[] {
    return deep(n) + 1000, "ok";
}

fn longdistance() {
    let v, msg = top(3);
    print(v);                            // 99
    print(msg);                          // boom
    print(deepr(5));                     // 5
}

// --- NRVO both ways ----------------------------------------------------------

fn build(n: int) -> u8[>..] {
    var out: u8[>..] = [];
    if n > 0 { out.push('a'); return out; }
    out.push('b');
    return out;                          // Same local: built at the destination.
}

fn pick_one(n: int) -> u8[>..] {
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

// --- casts and unsigned builtins ---------------------------------------------

fn casts() {
    print(300 as! u8);                   // 44
    print((0 - 1) as! u16);              // 65535 (`as` binds tighter than unary -)
    print(2.9 as! int);                  // 2 (checked `as` would abort in debug)
    print(-2.9 as! int);                 // -2
    print(7 as flt / 2.0);               // 3.5
    print(1 << 65);                      // 2 (shift count masked to 1)
    print(unsigned_shr(-1, 60));         // 15
    print(unsigned_div(-2, 3));          // 6148914691236517204
    print(unsigned_mod(-1, 10));         // 5
    print(unsigned_less(1, -1));         // true
    print(hardware_threads() >= 1);      // true
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

struct PSlot { n: int = 0 }

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

fn globals2() {
    print(gnext);                        // 42
    gtext.push('!');
    print(gtext);                        // g!
}

// --- empty and zero-sized ----------------------------------------------------

fn empties() {
    let e: int[] = [];
    print(e.len);                        // 0
    var g: flt[>..] = [];
    print(g.len);                        // 0
    let s: u8[:] = "";
    print(s.len);                        // 0
    print("");                           // (empty line)
}

// --- structs with resizable tails (§3.4) -------------------------------------

struct Tailed { count: int, items: int[>..] }

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

// --- print edges -------------------------------------------------------------

fn printedges() {
    print(-9223372036854775807 - 1);     // int64 min
    print(0.1);
    print(1.0e300);
    print(1.0 / 3.0);
}

fn main() {
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
    nrvo();
    casts();
    msgs();
    pool_reuse();
    globals2();
    empties();
    tails();
    seqvar();
    printedges();
}
