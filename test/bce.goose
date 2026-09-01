// Bounds-check elimination coverage. Every elide/keep annotation comment is
// verified by `goose --bce-test` (see run_tests.ps1): all checks on such a
// line must have exactly the annotated outcome. The file also runs as a
// normal codegen test, so a wrongly elided check would corrupt its output;
// every kept check passes at runtime. Keep-cases index with values loaded
// from array contents, which the analysis never tracks — and which no other
// test asserts or guards, since facts flow across inlined calls.

// --- fixed-size arrays -----------------------------------------------------

fn fixed_const() -> i64 {
    let a: i64[4] = [1, 2, 3, 4];
    return a[0] + a[3];                       // bce:elide
}

fn fixed_loop() -> i64 {
    let a: i64[4] = [1, 2, 3, 4];
    var s = 0;
    for i in a.len {
        s += a[i];                            // bce:elide
    }
    return s;
}

fn fixed_guarded(k: i64) -> i64 {
    let a: i64[4] = [5, 6, 7, 8];
    if k >= 0 && k < 4 {
        return a[k];                          // bce:elide
    }
    return 0;
}

fn fixed_match(k: i64) -> i64 {
    let a: i64[4] = [3, 5, 7, 9];
    match k {
        0..4 => a[k],                         // bce:elide
        _ => 0,
    }
}

// --- loops over dynamic arrays ---------------------------------------------

fn grow_loops() -> i64 {
    var v: i64[>..] = [];
    for i in 8 { v.push(i + 1); }
    var s = 0;
    for i in v.len {
        s += v[i];                            // bce:elide
    }
    for x, i in v {
        s += v[i] + x;                        // bce:elide
    }
    var j = 0;
    while j < v.len {
        s += v[j];                            // bce:elide
        j++;
    }
    var d = v.len;
    while d > 0 {
        d--;
        s += v[d];                            // bce:elide
    }
    return s;
}

// Growing during iteration: a grow-only array can never shrink, so bounds
// established before a push stay established.
fn grow_during_loop() -> i64 {
    var v: i64[>..] = [];
    v.push(1);
    v.push(2);
    var s = 0;
    for i in v.len {
        v.push(0);
        s += v[i];                            // bce:elide
    }
    guard v.len > 3 else { return s; }
    v.push(9);
    s += v[3];                                // bce:elide
    return s;
}

// --- facts from conditions -------------------------------------------------

fn last_elem(s: i64[:]) -> i64 {
    guard s.len > 0 else { return 0; }
    return s[s.len - 1];                      // bce:elide
}

fn asserted(s: i64[:], k: i64) -> i64 {
    assert(k >= 0);
    assert(k < s.len);
    return s[k];                              // bce:elide
}

fn unsigned_idx(s: i64[:], u: u16) -> i64 {
    if u < s.len {
        return s[u];                          // bce:elide
    }
    return 0;
}

fn early_out(s: i64[:], k: i64) -> i64 {
    if k < 0 || k >= s.len {
        return 0;
    }
    return s[k];                              // bce:elide
}

fn two_arrays(v: i64[:], w: i64[:]) -> i64 {
    let n = v.len;
    guard w.len == n else { return 0; }
    var s = 0;
    for i in n {
        s += v[i];                            // bce:elide
        s += w[i];                            // bce:elide
    }
    return s;
}

// --- slices ----------------------------------------------------------------

fn slice_whole(v: i64[:]) -> i64 {
    let a = v[0..v.len];                      // bce:elide
    let b = v[..];                            // bce:elide
    return a.len + b.len;
}

fn slice_guarded(s: i64[:], a: i64, b: i64) -> i64 {
    if a >= 0 && a <= b && b <= s.len {
        let t = s[a..b];                      // bce:elide
        return t.len;
    }
    return 0;
}

fn slice_unknown(s: i64[:], a: i64, b: i64) -> i64 {
    let t = s[a..b];                          // bce:keep
    return t.len;
}

// The spec's A.1 word scan: index and slice checks all disappear.
fn slice_word_scan(text: u8[:]) -> i64 {
    var chars = 0;
    var i = 0;
    while i < text.len {
        let start = i;
        while i < text.len && text[i] != ' ' {   // bce:elide
            i++;
        }
        let w = text[start..i];               // bce:elide
        chars += w.len;
        while i < text.len && text[i] == ' ' {   // bce:elide
            i++;
        }
    }
    return chars;
}

// --- limited and grow-shrink arrays ----------------------------------------

fn limited_ops(extra: i64) -> i64 {
    var l: i64[..8] = [1, 2, 3, 4];
    if extra > 0 { l.push(extra); }           // Length now unknown statically.
    var s = 0;
    for i in l.len {
        s += l[i];                            // bce:elide
    }
    l.pop();
    if l.len > 1 {
        s += l[1];                            // bce:elide
    }
    guard l.len > 0 else { return s; }
    l.pop();
    s += l[0];                                // bce:keep (the guard predates the pop)
    return s;
}

fn limited_conditional_shrink() -> i64 {
    var l: i64[..8] = [2, 4, 6, 8];
    var s = 0;
    for i in l.len {
        if l.len > 99 {
            l.pop();
        }
        s += l[i];                            // bce:keep (a body path may shrink l)
    }
    return s;
}

fn growshrink_loop() -> i64 {
    var g: i64[>..<] = [];
    g.push(4);
    g.push(5);
    g.push(6);
    var s = 0;
    for x, i in g {
        s += g[i] + x;                        // bce:elide
    }
    while g.len > 0 {
        s += g[g.len - 1];                    // bce:elide
        g.pop();
    }
    return s;
}

// --- growth and shrink vs established facts --------------------------------

fn append_bridge() -> i64 {
    var v: i64[>..] = [];
    v.push(7);
    v.push(8);
    v.push(9);
    v.push(10);
    guard v.len > 3 else { return 0; }
    v.push(11);
    return v[3];                              // bce:elide (growth keeps the bound)
}

// Exact lengths: literals, counted push loops, push/append deltas, resize.
fn exact_lengths() -> i64 {
    var v: i64[>..] = [];
    for i in 5 { v.push(i * 2); }
    var s = v[4];                             // bce:elide (five counted pushes)
    var l: i64[..8] = [1, 2, 3];
    s += l[2];                                // bce:elide (literal length)
    l.resize(6, 9);
    s += l[5];                                // bce:elide (resize sets the length)
    l.clear();
    if l.len == 0 { s += 1; }
    v.push(7);
    s += v[5];                                // bce:elide (push adds exactly one)
    let f: i64[3] = [4, 5, 6];
    v.append(f);
    s += v[8];                                // bce:elide (append adds exactly f.len)
    return s;
}

fn shrink_invalidates(extra: i64) -> i64 {
    var l: i64[..8] = [1, 2, 3, 4, 5];
    if extra > 0 { l.push(extra); }           // Length now unknown statically.
    guard l.len > 3 else { return 0; }
    l.pop();
    return l[3];                              // bce:keep (the pop may undo the guard)
}

fn whole_assign_kills(extra: i64) -> i64 {
    var v: i64[>..] = [];
    v.push(1);
    v.push(2);
    v.push(3);
    var w: i64[>..] = [];
    if extra > 0 { w.push(60); }              // Length of w unknown statically.
    w.push(5);
    w.push(6);
    guard v.len > 1 else { return 0; }
    v = w;
    return v[1];                              // bce:keep (assignment resizes v)
}

// --- value ranges from the operation itself --------------------------------

// `%` is Euclidean (§6.2), so the reduction is in range for any dividend --
// no guard, and nothing left for a check to catch.
fn modulo_index(s: i64[:], k: i64) -> i64 {
    guard s.len > 0 else { return 0; }
    return s[k % s.len];                      // bce:elide
}

// A negative divisor bounds the result by its magnitude, which the analysis
// can state when the divisor is a constant...
fn modulo_negative_const(s: i64[:], k: i64) -> i64 {
    guard s.len > 3 else { return 0; }
    return s[k % -4];                         // bce:elide
}

// ...but not when it is a symbolic negation: |b| is not expressible as an
// offset from b's own term. The access is in fact safe.
fn modulo_negative(s: i64[:], k: i64) -> i64 {
    guard s.len > 0 else { return 0; }
    let n = 0 - s.len;
    return s[k % n];                          // bce:keep
}

fn mask_index() -> i64 {
    var t: i64[>..] = [];
    for i in 64 { t.push(i); }
    var s = 0;
    var h: u64 = 0x9e3779b97f4a7c15;
    for i in 8 {
        h = h * 6364136223846793005 + 1442695040888963407;
        s += t[(h & 63) as! i64];             // bce:elide (a mask bounds its result)
    }
    return s;
}

fn unsigned_modulo(s: i64[:], r: u64) -> i64 {
    guard s.len > 0 else { return 0; }
    let i = (r % s.len as u64) as! i64;       // The idiomatic random-index reduction.
    return s[i];                              // bce:elide
}

// --- calls: what a callee can and cannot reach ------------------------------

recursive fn opaque_call(x: i64) -> i64 {
    if x <= 0 {
        return 0;
    }
    return opaque_call(x - 1) + 1;
}

// A callee can never reach a local whose address is not taken.
fn call_local_loop() -> i64 {
    var v: i64[>..] = [];
    for i in 6 { v.push(i); }
    var s = 0;
    for i in v.len {
        s += opaque_call(v[i]);               // bce:elide (the call cannot reach v)
    }
    return s;
}

fn reset_it(p: i64[>..]&) {
    p = [];
}

fn call_shrink_loop() -> i64 {
    var v: i64[>..] = [];
    for i in 6 { v.push(i + 2); }
    var s = 0;
    for i in v.len {
        s += v[i];                            // bce:keep (reset_it may empty v)
        if v.len > 99 { reset_it(&v); }
    }
    return s;
}

var gv: i64[>..] = [];

fn global_loop() -> i64 {
    gv.push(11);
    gv.push(22);
    var s = 0;
    for i in gv.len {
        s += gv[i];                           // bce:elide (no calls in the body)
    }
    for i in gv.len {
        s += opaque_call(gv[i]);              // bce:keep (a call may mutate a global)
    }
    return s;
}

// --- globals as cursor state ------------------------------------------------

// A cursor whose lower bound exists nowhere in the function: it comes from
// the whole-program invariant that gpos is never assigned a negative value.
var gpos = 0;
var gtext: u8[>..] = [];

fn gscan() -> i64 {
    var words = 0;
    while gpos < gtext.len {
        var n = 0;
        while gpos < gtext.len && gtext[gpos] != ' ' {   // bce:elide
            n++;
            gpos++;
        }
        if n > 0 { words++; }
        while gpos < gtext.len && gtext[gpos] == ' ' { gpos++; }   // bce:elide
    }
    return words;
}

// A global the program can drive negative keeps its checks everywhere.
var gcursor = 0;

fn gcursor_back(back: i64) {
    gcursor = gcursor - back;
}

fn gcursor_read(s: i64[:]) -> i64 {
    if gcursor < s.len {
        return s[gcursor];                    // bce:keep (gcursor may be negative)
    }
    return 0;
}

// --- growth does not invalidate an index invariant --------------------------

fn grown_then_scanned() -> i64 {
    var buf: u8[>..] = [];
    buf.append("ab cd  e");
    var count = 0;
    var i = 0;
    while i < buf.len {
        let start = i;
        while i < buf.len && buf[i] != ' ' { i++; }   // bce:elide
        let w = buf[start..i];                        // bce:elide
        count += w.len;
        while i < buf.len && buf[i] == ' ' { i++; }   // bce:elide
    }
    return count;
}

fn sum_through_ref(p: i64[>..]&) -> i64 {
    var s = 0;
    for i in p.len {
        s += p[i];                            // bce:elide
    }
    return s;
}

// --- places beyond plain locals --------------------------------------------

struct Holder {
    a: i64[..8],
    tag: i64,
}

fn field_place() -> i64 {
    var h = Holder { [1, 2, 3], 7 };
    var s = 0;
    for i in h.a.len {
        s += h.a[i];                          // bce:elide
    }
    return s + h.tag;
}

fn pool_loop() -> i64 {
    reusable var pool: i64[>..] = [];
    let i0 = pool.alloc_index(10);
    let i1 = pool.alloc_index(20);
    pool.free(i0);
    let i2 = pool.alloc_index(30);
    var s = i0 + i1 + i2;
    for i in pool.len {
        s += pool[i];                         // bce:elide (alloc/free never shrink)
    }
    return s;
}

// --- known limitations and plain keeps -------------------------------------

fn keep_unknown(s: i64[:], k: i64) -> i64 {
    return s[k];                              // bce:keep
}

fn keep_upper_only(s: i64[:], k: i64) -> i64 {
    if k < s.len {
        return s[k];                          // bce:keep (no lower bound on k)
    }
    return 0;
}

fn keep_lower_only(s: i64[:], k: i64) -> i64 {
    if k >= 0 {
        return s[k];                          // bce:keep (no upper bound on k)
    }
    return 0;
}

fn keep_off_by_one(s: i64[:], k: i64) -> i64 {
    if k >= 0 && k <= s.len {
        return s[k];                          // bce:keep (k == len would be out of bounds)
    }
    return 0;
}

fn keep_cross_array(v: i64[:], w: i64[:]) -> i64 {
    var s = 0;
    for i in v.len {
        s += w[i];                            // bce:keep (bounded by the other array)
    }
    return s;
}

// Facts are not collected from `i + 1 < len` (the sum may have wrapped
// before the compare); spell it `i < len - 1` instead.
fn plus_one_limitation(s: i64[:]) -> i64 {
    var r = 0;
    var i = 0;
    while i + 1 < s.len {
        r += s[i + 1];                        // bce:keep
        i++;
    }
    return r;
}

fn plus_one_provable(s: i64[:]) -> i64 {
    var r = 0;
    for i in s.len - 1 {
        r += s[i + 1];                        // bce:elide
    }
    return r;
}

// --- driver ----------------------------------------------------------------

fn main() {
    var data: i64[>..] = [];
    for i in 8 { data.push(i + 1); }          // 1..8
    var data2: i64[>..] = [];
    for i in 8 { data2.push(i * 2); }         // 0..14
    let ks = data[1];                         // 2, opaque to the analysis
    let ke = data[4];                         // 5, opaque to the analysis
    let kk = data2[1];                        // 2, opaque; keep-cases only
    let ka = data2[1];                        // 2, opaque; keep-cases only
    let kb = data2[2];                        // 4, opaque; keep-cases only
    let kz = data2[0];                        // 0, opaque; keep-cases only

    print(fixed_const());                     // 5
    print(fixed_loop());                      // 10
    print(fixed_guarded(ks));                 // 7
    print(fixed_match(ks));                   // 7
    print(grow_loops());                      // 180
    print(grow_during_loop());                // 3
    print(last_elem(data));                   // 8
    print(asserted(data, ks));                // 3
    print(unsigned_idx(data, 2));             // 3
    print(early_out(data, ks));               // 3
    print(two_arrays(data, data2));           // 92
    print(slice_whole(data));                 // 16
    print(slice_guarded(data, ks, ke));       // 3
    print(slice_unknown(data, ka, kb));       // 2
    print(slice_word_scan("hello world  x")); // 11
    print(limited_ops(kz));                   // 13
    print(limited_conditional_shrink());      // 20
    print(growshrink_loop());                 // 45
    print(append_bridge());                   // 10
    print(exact_lengths());                   // 34
    print(shrink_invalidates(kz));            // 4
    print(whole_assign_kills(kz));            // 6
    let kneg = 0 - data[2];                   // -3, opaque to the analysis
    print(modulo_index(data, ke));            // 6
    print(modulo_index(data, kneg));          // 6 (Euclidean: -3 % 8 == 5)
    print(modulo_negative_const(data, kneg)); // 2 (-3 % -4 == 1)
    print(modulo_negative(data, ke));         // 6
    print(mask_index());                      // 228
    print(unsigned_modulo(data, 20));         // 5
    print(call_local_loop());                 // 15
    print(call_shrink_loop());                // 27
    print(global_loop());                     // 66
    gtext.append("one two  three ");
    print(gscan());                           // 3
    gcursor_back(kz);
    print(gcursor_read(data));                // 1
    print(grown_then_scanned());              // 5
    print(sum_through_ref(&data));            // 36
    print(field_place());                     // 13
    print(pool_loop());                       // 51
    print(keep_unknown(data, kk));            // 3
    print(keep_upper_only(data, kk));         // 3
    print(keep_lower_only(data, kk));         // 3
    print(keep_off_by_one(data, kk));         // 3
    let short = data2[0..3];
    print(keep_cross_array(short, data));     // 6
    print(plus_one_limitation(data));         // 35
    print(plus_one_provable(data));           // 35
}
