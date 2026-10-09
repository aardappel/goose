// The Goose standard library: `std` (docs/stdlib.md; the design is
// docs/design/stdlib_design.md).
// Functions over the language's own types; nothing here allocates, and
// nothing non-fixed is ever taken by value (spec §4.1). Fresh arrays come
// back as T[>..] and land in the caller's destination (§7.3); "maybe an
// element" is a T? into the input; positions are i64 with -1 for none.

// ---- scalars and bits --------------------------------------------------

fn min<T>(a: T, b: T) -> T { if b < a { return b; } return a; }
fn max<T>(a: T, b: T) -> T { if a < b { return b; } return a; }
fn clamp<T>(x: T, lo: T, hi: T) -> T {
    if x < lo { return lo; }
    if hi < x { return hi; }
    return x;
}

fn abs<T>(x: T) -> T { if x < 0 { return -x; } return x; }
fn abs(x: f32) -> f32 { if x < 0.0 { return -x; } return x; }
fn abs(x: f64) -> f64 { if x < 0.0 { return -x; } return x; }

fn sign<T>(x: T) -> T {
    if x < 0 { return -1; }
    if 0 < x { return 1; }
    return 0;
}
fn sign(x: f32) -> f32 {
    if x < 0.0 { return -1.0; }
    if 0.0 < x { return 1.0; }
    return 0.0;
}
fn sign(x: f64) -> f64 {
    if x < 0.0 { return -1.0; }
    if 0.0 < x { return 1.0; }
    return 0.0;
}

fn lerp(a: f32, b: f32, t: f32) -> f32 { a + (b - a) * t }
fn lerp(a: f64, b: f64, t: f64) -> f64 { a + (b - a) * t }

// The smallest power of two that is at least max(x, 1).
fn next_pow2(x: i64) -> i64 {
    var p = 1;
    while p < x { p = p * 2; }
    return p;
}

fn popcount(x: u64) -> i64 {
    var v = x;
    var n = 0;
    while v != 0 {
        v = v & (v - 1);
        n += 1;
    }
    return n;
}

// Leading / trailing zero bits; 64 for zero.
fn clz(x: u64) -> i64 {
    if x == 0 { return 64; }
    var v = x;
    var n = 0;
    while (v >> 63) == 0 {
        v = v << 1;
        n += 1;
    }
    return n;
}
fn ctz(x: u64) -> i64 {
    if x == 0 { return 64; }
    var v = x;
    var n = 0;
    while (v & 1) == 0 {
        v = v >> 1;
        n += 1;
    }
    return n;
}

// The values a and b refer to. `T&` of a reference type is that reference
// (spec §3.8), so for two elements of an array of references this swaps
// what they point to; swap_at swaps the elements themselves.
fn swap<T>(a: T&, b: T&) {
    let t = a;
    a = b;
    b = t;
}

// ---- hashing -------------------------------------------------------------
// One overload per scalar type: a u8 widens equally well into i64 and u64,
// so a single generic would be ambiguous. Integers get a multiply-xorshift
// mix (the low bits are what `dictionary` masks), bytes FNV-1a. A negative
// integer mixes as its two's-complement bits, and a float as its count of
// 1/65536ths, truncated and wrapped to 64 bits: `as!`, since those
// conversions change the value on purpose.

fn hash_mix(x: u64) -> u64 {
    var h = x * 11400714819323198485;
    h = h ^ (h >> 32);
    return h;
}

fn hash(x: i8) -> u64 { hash_mix(x as! u64) }
fn hash(x: i16) -> u64 { hash_mix(x as! u64) }
fn hash(x: i32) -> u64 { hash_mix(x as! u64) }
fn hash(x: i64) -> u64 { hash_mix(x as! u64) }
fn hash(x: u8) -> u64 { hash_mix(x) }
fn hash(x: u16) -> u64 { hash_mix(x) }
fn hash(x: u32) -> u64 { hash_mix(x) }
fn hash(x: u64) -> u64 { hash_mix(x) }
fn hash(x: bool) -> u64 { if x { return hash_mix(1); } return hash_mix(2); }
fn hash(x: f32) -> u64 { hash_mix((x * 65536.0) as! u64) }
fn hash(x: f64) -> u64 { hash_mix((x * 65536.0) as! u64) }

fn hash(s: u8[:]) -> u64 {
    var h: u64 = 14695981039346656037;
    for b in s { h = (h ^ b) * 1099511628211; }
    return h;
}

fn hash_combine(seed: u64, h: u64) -> u64 {
    seed ^ (h + 11400714819323198485 + (seed << 6) + (seed >> 2))
}

// ---- random numbers ---------------------------------------------------------
// splitmix64: one word of state, which is also the seed.

struct rng { s: u64 }

fn rand_u64(r: rng&) -> u64 {
    r.s = r.s + 11400714819323198485;
    var z = r.s;
    z = (z ^ (z >> 30)) * 13787848793156543929;
    z = (z ^ (z >> 27)) * 10723151780598845931;
    return z ^ (z >> 31);
}

// Uniform in [0, n).
fn rand_int(r: rng&, n: i64) -> i64 { (rand_u64(r) % (n as u64)) as i64 }

// Uniform in [0, 1).
fn rand_flt(r: rng&) -> f64 { (rand_u64(r) >> 11) * (1.0 / 9007199254740992.0) }

// Fisher-Yates.
// The functions that move elements around store whole elements with `.=`,
// which moves a reference element itself rather than writing through it
// (spec §3.8), and hold one in a `T` local, which keeps a reference.
fn shuffle<T>(xs: T[:], r: rng&) {
    var i = xs.len - 1;
    while i > 0 {
        let j = rand_int(r, i + 1);
        let t: T = xs[i];
        xs[i] .= xs[j];
        xs[j] .= t;
        i -= 1;
    }
}

// ---- arrays: reading and searching -------------------------------------------

fn swap_at<T>(xs: T[:], i: i64, j: i64) {
    let t: T = xs[i];
    xs[i] .= xs[j];
    xs[j] .= t;
}

// F(x), last to first.
fn each_rev<T, F>(xs: T[:]) {
    var i = xs.len;
    while i > 0 {
        i -= 1;
        F(xs[i]);
    }
}

// F(chunk, i) over consecutive slices of n elements; the last may be short.
fn each_chunk<T, F>(xs: T[:], n: i64) {
    var i = 0;
    var k = 0;
    while i < xs.len {
        let e = min(i + n, xs.len);
        F(xs[i..e], k);
        i = e;
        k += 1;
    }
}

// The first x with F(x), as a reference into xs; null if none.
fn find<T, F>(xs: T[:]) -> T? {
    for &x in xs { if F(x) { return x; } }
    return null;
}

fn find_index<T, F>(xs: T[:]) -> i64 {
    for x, i in xs { if F(x) { return i; } }
    return -1;
}

// The first i with xs[i] == v, -1 if none. (`index_of` is the builtin that
// turns an element reference into its index.)
fn position<T>(xs: T[:], v: T) -> i64 {
    for x, i in xs { if x == v { return i; } }
    return -1;
}

fn contains<T>(xs: T[:], v: T) -> bool { position(xs, v) >= 0 }

fn any<T, F>(xs: T[:]) -> bool {
    for x in xs { if F(x) { return true; } }
    return false;
}

fn all<T, F>(xs: T[:]) -> bool {
    for x in xs { if !F(x) { return false; } }
    return true;
}

fn count<T, F>(xs: T[:]) -> i64 {
    var n = 0;
    for x in xs { if F(x) { n += 1; } }
    return n;
}

fn min<T>(xs: T[:]) -> T {
    assert(xs.len > 0);
    var m = xs[0];
    for x in xs { if x < m { m = x; } }
    return m;
}
fn min<T, F>(xs: T[:]) -> T {
    assert(xs.len > 0);
    var m = xs[0];
    for x in xs { if F(x, m) { m = x; } }
    return m;
}
fn max<T>(xs: T[:]) -> T {
    assert(xs.len > 0);
    var m = xs[0];
    for x in xs { if m < x { m = x; } }
    return m;
}
fn max<T, F>(xs: T[:]) -> T {
    assert(xs.len > 0);
    var m = xs[0];
    for x in xs { if F(m, x) { m = x; } }
    return m;
}

// The index of the element with the smallest / largest key F(x); -1 if empty.
fn min_by<T, F>(xs: T[:]) -> i64 {
    if xs.len == 0 { return -1; }
    var best = 0;
    var bk = F(xs[0]);
    for x, i in xs {
        let k = F(x);
        if k < bk { bk = k; best = i; }
    }
    return best;
}
fn max_by<T, F>(xs: T[:]) -> i64 {
    if xs.len == 0 { return -1; }
    var best = 0;
    var bk = F(xs[0]);
    for x, i in xs {
        let k = F(x);
        if bk < k { bk = k; best = i; }
    }
    return best;
}

fn last<T>(xs: T[:]) -> T& {
    assert(xs.len > 0);
    xs[xs.len - 1]
}

// The first i with !F(xs[i]), where F holds on a prefix of xs.
fn lower_bound<T, F>(xs: T[:]) -> i64 {
    var lo = 0;
    var hi = xs.len;
    while lo < hi {
        let mid = lo + (hi - lo) / 2;
        if F(xs[mid]) { lo = mid + 1; } else { hi = mid; }
    }
    return lo;
}

// xs sorted by <: the position of v (or where it would go), and whether it is there.
fn binary_search<T>(xs: T[:], v: T) -> i64, bool {
    var lo = 0;
    var hi = xs.len;
    while lo < hi {
        let mid = lo + (hi - lo) / 2;
        if xs[mid] < v { lo = mid + 1; } else { hi = mid; }
    }
    return lo, lo < xs.len && xs[lo] == v;
}

// The first / last occurrence of a subsequence, -1 if none.
fn find<T>(xs: T[:], sub: T[:]) -> i64 {
    if sub.len == 0 { return 0; }
    var i = 0;
    while i + sub.len <= xs.len {
        if xs[i..i + sub.len] == sub { return i; }
        i += 1;
    }
    return -1;
}
fn rfind<T>(xs: T[:], sub: T[:]) -> i64 {
    if sub.len > xs.len { return -1; }
    var i = xs.len - sub.len;
    while i >= 0 {
        if xs[i..i + sub.len] == sub { return i; }
        i -= 1;
    }
    return -1;
}

fn starts_with<T>(xs: T[:], p: T[:]) -> bool { p.len <= xs.len && xs[..p.len] == p }
fn ends_with<T>(xs: T[:], p: T[:]) -> bool { p.len <= xs.len && xs[xs.len - p.len..] == p }

// ---- arrays: transforming ------------------------------------------------------

fn map<T, F>(xs: T[:]) {
    var out = [];
    for x in xs { out.push(F(x)); }
    return out;
}

fn filter<T, F>(xs: T[:]) -> T[>..] {
    var out: T[>..] = [];
    for x in xs { if F(x) { out.push(copy(x)); } }
    return out;
}

// acc = F(acc, x) for each x, from the seed.
fn fold<T, A, F>(xs: T[:], acc: A) -> A {
    var a = acc;
    for x in xs { a = F(a, x); }
    return a;
}

fn sum<T>(xs: T[:]) -> T {
    var s = default<T>();
    for x in xs { s += x; }
    return s;
}

fn concat<T>(a: T[:], b: T[:]) -> T[>..] {
    var out: T[>..] = [];
    out.append(a);
    out.append(b);
    return out;
}

// ---- arrays: in place -------------------------------------------------------------

fn fill<T>(xs: T[:], v: T) { for i in xs.len { xs[i] .= v; } }

// Element by element, first to last; the lengths must agree.
fn copy_into<T>(dst: T[:], src: T[:]) {
    assert(dst.len == src.len);
    for i in dst.len { dst[i] .= src[i]; }
}

fn reverse<T>(xs: T[:]) {
    var i = 0;
    var j = xs.len - 1;
    while i < j {
        swap_at(xs, i, j);
        i += 1;
        j -= 1;
    }
}

// ASCII, in place.
fn to_lower(s: u8[:]) { for &c in s { if c >= 'A' && c <= 'Z' { c += 32; } } }
fn to_upper(s: u8[:]) { for &c in s { if c >= 'a' && c <= 'z' { c -= 32; } } }

// ---- arrays: sorting -----------------------------------------------------------
// The sort_ helpers take the comparator as F, "a goes before b", and are
// only meant to be called from sort and stable_sort. Every index they use
// stays in range whatever F returns, so a comparator that is no strict weak
// ordering leaves some permutation of the input rather than an abort.

// Insertion sort. Stable: an element moves only past those it goes strictly
// before.
fn sort_insertion<T, F>(v: T[:]) {
    for i in 1..v.len {
        let x: T = v[i];
        var j = i;
        while j > 0 {
            let y: T = v[j - 1];
            if !F(x, y) { break; }
            v[j] .= y;
            j -= 1;
        }
        v[j] .= x;
    }
}

// The length of the run v starts with: elements in order, or in strictly
// descending order, which are reversed in place so that they are in order
// too. Reversing a strictly descending run keeps a stable sort stable.
fn sort_run<T, F>(v: T[:]) -> i64 {
    if v.len < 2 { return v.len; }
    var e = 2;
    if F(v[1], v[0]) {
        while e < v.len {
            if !F(v[e], v[e - 1]) { break; }
            e += 1;
        }
        reverse(v[..e]);
    } else {
        while e < v.len {
            if F(v[e], v[e - 1]) { break; }
            e += 1;
        }
    }
    return e;
}

// The index of the median of v[a], v[b] and v[c].
fn sort_median3<T, F>(v: T[:], a: i64, b: i64, c: i64) -> i64 {
    let x = F(v[a], v[b]);
    if x != F(v[a], v[c]) { return a; }
    // v[a] goes before both others (x) or after both: the median is the
    // first of the other two, or the last.
    if F(v[b], v[c]) != x { return c; }
    return b;
}

// A pivot for v, at least 8 long: the median of elements at 0, 4/8 and 7/8
// of the way, and from 128 elements on the median of three such medians,
// each over its own eighth of v.
fn sort_pivot<T, F>(v: T[:]) -> i64 {
    let n8 = v.len / 8;
    let a = 0;
    let b = n8 * 4;
    let c = n8 * 7;
    if v.len < 128 { return sort_median3(v, a, b, c) { x, y => F(x, y) }; }
    let m8 = n8 / 8;
    let ma = sort_median3(v, a, a + m8 * 4, a + m8 * 7) { x, y => F(x, y) };
    let mb = sort_median3(v, b, b + m8 * 4, b + m8 * 7) { x, y => F(x, y) };
    let mc = sort_median3(v, c, c + m8 * 4, c + m8 * 7) { x, y => F(x, y) };
    return sort_median3(v, ma, mb, mc) { x, y => F(x, y) };
}

// Moves the elements x of v[1..] with F(x, v[0]) to the front of v[1..] and
// returns how many there are; v[0] is the pivot and stays. Every step swaps
// and advances the boundary by the comparison's outcome, so the loop has no
// branch on the data (a branchless Lomuto partition).
fn sort_partition<T, F>(v: T[:]) -> i64 {
    let p: T = v[0];
    var lt = 1;
    for i in 1..v.len {
        let x: T = v[i];
        v[i] .= v[lt];
        v[lt] .= x;
        lt += if F(x, p) { 1 } else { 0 };
    }
    return lt - 1;
}

// Heapsort: O(n log n) whatever the input, for ranges partitioning keeps
// splitting badly.
fn sort_heap<T, F>(v: T[:]) {
    var i = v.len / 2;
    while i > 0 {
        i -= 1;
        sift_down(v, i) { a, b => F(b, a) };
    }
    var end = v.len;
    while end > 1 {
        end -= 1;
        swap_at(v, 0, end);
        sift_down(v[..end], 0) { a, b => F(b, a) };
    }
}

// Unstable, in place, no allocation: a pattern-defeating quicksort after
// pdqsort and Rust's ipnsort. Input already in order, or in strictly
// descending order, takes one pass. Otherwise each round partitions a range
// around a median pivot without branching on the comparisons, defers the
// larger side on a stack of ranges (at most log2(n) of them) and goes on
// with the smaller one. Everything left of a range goes before or with
// everything in it, so a range whose pivot does not go after the element
// just before the range has nothing that goes before the pivot: one
// partition moves the elements equal to the pivot to the front, where they
// are done, and many equal keys cost linear passes rather than n log n. A
// range still being partitioned 2 log2(n) rounds deep is heapsorted, which
// bounds the worst case at O(n log n). Ranges of 10 or fewer are insertion
// sorted: its mispredicted loop exits make it slower than partitioning
// above that.
fn sort<T>(xs: T[:]) { sort(xs) { a, b => a < b } }
fn sort<T, F>(xs: T[:]) {
    let n = xs.len;
    if n <= 10 {
        sort_insertion(xs) { a, b => F(a, b) };
        return;
    }
    let r = sort_run(xs) { a, b => F(a, b) };
    if r == n { return; }
    var limit = 2 * (63 - clz(n as u64));
    var st: i64[..144] = [];   // lo, hi, limit per deferred range
    var lo = 0;
    var hi = n;
    loop {
        let v = xs[lo..hi];
        if v.len > 10 && limit > 0 {
            limit -= 1;
            swap_at(v, 0, sort_pivot(v) { a, b => F(a, b) });
            if lo > 0 && !F(xs[lo - 1], v[0]) {
                let m = sort_partition(v) { a, b => !F(b, a) };
                swap_at(v, 0, m);
                lo += m + 1;
                continue;
            }
            let m = sort_partition(v) { a, b => F(a, b) };
            swap_at(v, 0, m);
            if m < v.len - 1 - m {
                st.push(lo + m + 1);
                st.push(hi);
                st.push(limit);
                hi = lo + m;
            } else {
                st.push(lo);
                st.push(lo + m);
                st.push(limit);
                lo += m + 1;
            }
            continue;
        }
        if v.len > 10 {
            sort_heap(v) { a, b => F(a, b) };
        } else {
            sort_insertion(v) { a, b => F(a, b) };
        }
        if st.len == 0 { return; }
        limit = st.pop();
        hi = st.pop();
        lo = st.pop();
    }
}

// Merges the sorted runs l and r into d, as long as both; on ties l's
// element comes first. Runs already in order are copied. Where the output
// switches between the runs often, as on random input, a branch on each
// comparison mispredicts about every other step, so the step selects its
// element and side with conditional moves instead. Where it rarely
// switches, a predicted branch runs ahead of the comparisons, which matters
// most for comparators that load what they compare. `chaotic` chooses the
// form, and the merge leaves its own verdict in it for the next one.
fn sort_merge<T, F>(l: T[:], r: T[:], d: T[:], chaotic: bool&) {
    if !F(r[0], l[l.len - 1]) {
        copy_into(d[..l.len], l);
        copy_into(d[l.len..], r);
        return;
    }
    var a = 0;
    var b = 0;
    var k = 0;
    var switches = 0;
    var last_r = false;
    if chaotic {
        while a < l.len && b < r.len {
            let take_r = F(r[b], l[a]);
            d[k] .= if take_r { r[b] } else { l[a] };
            if take_r { b += 1; } else { a += 1; }
            if take_r != last_r { switches += 1; }
            last_r = take_r;
            k += 1;
        }
    } else {
        while a < l.len && b < r.len {
            if F(r[b], l[a]) {
                d[k] .= r[b];
                b += 1;
                if !last_r { switches += 1; }
                last_r = true;
            } else {
                d[k] .= l[a];
                a += 1;
                if last_r { switches += 1; }
                last_r = false;
            }
            k += 1;
        }
    }
    chaotic = switches * 4 > k;
    if a < l.len {
        copy_into(d[k..], l[a..]);
    } else {
        copy_into(d[k..], r[b..]);
    }
}

// Stable: a natural merge sort. One pass finds the runs, elements in order
// or in strictly descending order (reversed), extending each run shorter
// than 16 by insertion sort; then passes merge neighboring runs in pairs,
// alternating between xs and one temporary of xs.len elements on a fresh
// stack, so that no pass copies back. Input that is one run takes the first
// pass alone, and 20 or fewer elements are insertion sorted; neither
// allocates.
fn stable_sort<T>(xs: T[:]) { stable_sort(xs) { a, b => a < b } }
fn stable_sort<T, F>(xs: T[:]) {
    let n = xs.len;
    if n <= 20 {
        sort_insertion(xs) { a, b => F(a, b) };
        return;
    }
    var r = sort_run(xs) { a, b => F(a, b) };
    if r == n { return; }
    // The end of every run, in order.
    var ends: i64[>..] = [];
    var lo = 0;
    loop {
        let v = xs[lo..];
        if r < 16 && r < v.len {
            r = min(16, v.len);
            sort_insertion(v[..r]) { a, b => F(a, b) };
        }
        lo += r;
        ends.push(lo);
        if lo >= n { break; }
        r = sort_run(xs[lo..]) { a, b => F(a, b) };
    }
    var tmp: T[>..] = [];
    tmp.append(xs);
    var into_tmp = true;
    var chaotic = true;
    while ends.len > 1 {
        let src: T[:] = if into_tmp { xs } else { tmp };
        let dst: T[:] = if into_tmp { tmp } else { xs };
        var start = 0;
        var w = 0;
        var i = 0;
        while i < ends.len {
            let mid = ends[i];
            var end = mid;
            if i + 1 < ends.len {
                end = ends[i + 1];
                sort_merge(src[start..mid], src[mid..end], dst[start..end], chaotic) { a, b => F(a, b) };
            } else {
                copy_into(dst[start..end], src[start..end]);
            }
            ends[w] = end;
            w += 1;
            start = end;
            i += 2;
        }
        ends.resize(w);
        into_tmp = !into_tmp;
    }
    if !into_tmp { copy_into(xs, tmp); }
}

// ---- arrays: changing the length -------------------------------------------------
// `xs: A&` binds the whole array, so each instantiates only for array kinds
// that have the operations it uses.

fn push_n<A, T>(xs: A&, v: T, n: i64) { for i in n { xs.push(v); } }

fn insert_at<A, T>(xs: A&, i: i64, v: T) {
    xs.push(v);
    var j = xs.len - 1;
    while j > i {
        xs[j] .= xs[j - 1];
        j -= 1;
    }
    xs[i] .= v;
}

// Removes and returns xs[i], shifting the rest down. The result is a copy
// of the value, as `let v = xs[i]` makes one (of the pointee, for a
// reference element), here and in swap_remove and heap_pop.
fn remove_at<A>(xs: A&, i: i64) {
    let v = xs[i];
    var j = i;
    while j + 1 < xs.len {
        xs[j] .= xs[j + 1];
        j += 1;
    }
    xs.pop();
    return v;
}

// Removes and returns xs[i] in O(1), moving the last element into its place.
fn swap_remove<A>(xs: A&, i: i64) {
    let v = xs[i];
    xs[i] .= xs[xs.len - 1];
    xs.pop();
    return v;
}

// Keeps the elements with F(x), in order.
fn retain<A, F>(xs: A&) {
    var w = 0;
    for i in xs.len {
        if F(xs[i]) {
            xs[w] .= xs[i];
            w += 1;
        }
    }
    xs.resize(w);
}

// Drops adjacent duplicates (sort first to drop all).
fn dedup<A>(xs: A&) {
    if xs.len == 0 { return; }
    var w = 1;
    var i = 1;
    while i < xs.len {
        if !(xs[i] == xs[w - 1]) {
            xs[w] .= xs[i];
            w += 1;
        }
        i += 1;
    }
    xs.resize(w);
}

// A binary min-heap by < (or by F(a, b), "a goes before b"). The sifts move
// a hole rather than swapping: the element being placed waits in a local,
// each level moves one element into the hole, and the element is stored once
// where the hole stops. For a comparator that is a strict weak order, every
// operation leaves the layout that swapping the element along would.
fn heap_push<A, T>(xs: A&, v: T) { heap_push(xs, v) { a, b => a < b } }
fn heap_push<A, T, F>(xs: A&, v: T) {
    xs.push(v);
    sift_up(xs, xs.len - 1) { a, b => F(a, b) };
}

// Moves xs[start] towards the root past every element it goes before. The
// element is read back from the array rather than taken from heap_push's v,
// whose type may differ from the elements' (an i32 pushed into i64s), and
// held in a T, the element type, which keeps a reference element itself.
fn sift_up<T, F>(xs: T[:], start: i64) {
    var i = start;
    let x: T = xs[i];
    while i > 0 {
        let p = (i - 1) / 2;
        if !F(x, xs[p]) { break; }
        xs[i] .= xs[p];
        i = p;
    }
    xs[i] .= x;
}

fn sift_down<T, F>(xs: T[:], start: i64) {
    let n = xs.len;
    var i = start;
    if 2 * i + 1 >= n { return; }   // A leaf, or a start past the end, is not read.
    let x: T = xs[i];
    loop {
        let l = 2 * i + 1;
        if l >= n { break; }
        var m = l;
        let r = l + 1;
        if r < n && F(xs[r], xs[l]) { m = r; }
        if !F(xs[m], x) { break; }
        xs[i] .= xs[m];
        i = m;
    }
    xs[i] .= x;
}

// The hole left at the root walks down to a leaf along the child that goes
// first, and the last element then sifts up from there, as in libstdc++ and
// Rust: the last element nearly always belongs near the bottom, so this
// takes about one comparison per level where sifting it down takes two.
fn heap_pop<A>(xs: A&) { heap_pop(xs) { a, b => a < b } }
fn heap_pop<A, F>(xs: A&) {
    let top = xs[0];
    sift_hole(xs) { a, b => F(a, b) };
    // Popped after the sift: for an array of references the checker rejects
    // a pop that a use of the array follows (implementation.md §10).
    xs.pop();
    return top;
}

// Fills the hole heap_pop leaves at the root of xs[..n] with x = xs[n], the
// element it pops off the end afterwards; x is held in a T, the element
// type, which keeps a reference element itself.
fn sift_hole<T, F>(xs: T[:]) {
    let n = xs.len - 1;
    if n == 0 { return; }
    let x: T = xs[n];
    var i = 0;
    var c = 1;
    while c < n - 1 {
        if F(xs[c + 1], xs[c]) { c += 1; }
        xs[i] .= xs[c];
        i = c;
        c = 2 * c + 1;
    }
    if c == n - 1 {
        xs[i] .= xs[c];
        i = c;
    }
    // Going up, x passes every element that does not go before it, ties
    // included, and stops below the first that does: where sifting it down
    // from the root would have stopped.
    while i > 0 {
        let p = (i - 1) / 2;
        if F(xs[p], x) { break; }
        xs[i] .= xs[p];
        i = p;
    }
    xs[i] .= x;
}

fn make_heap<A>(xs: A&) { make_heap(xs) { a, b => a < b } }
fn make_heap<A, F>(xs: A&) {
    var i = xs.len / 2;
    while i > 0 {
        i -= 1;
        sift_down(xs, i) { a, b => F(a, b) };
    }
}

// ---- strings ---------------------------------------------------------------------
// Plain rendering is the builtin `format`; these add control and parsing.

// Digits of v in the base (2..36), right-aligned in `width` with `pad`.
// Base ten has its own loop: dividing by the constant is a multiply, where
// dividing by the `base` parameter is a real division per digit.
fn format_uint(out: u8[>..]&, v: u64, base: i64, width: i64, padc: u8) {
    var digits: u8[..64] = [];
    var x = v;
    if x == 0 { digits.push('0'); }
    if base == 10 {
        while x > 0 {
            digits.push(('0' + (x % 10) as i64) as u8);
            x = x / 10;
        }
    } else {
        let b = base as u64;
        while x > 0 {
            let d = (x % b) as i64;
            var c: u8 = 0;
            if d < 10 { c = ('0' + d) as u8; } else { c = ('a' + d - 10) as u8; }
            digits.push(c);
            x = x / b;
        }
    }
    var padn = width - digits.len;
    while padn > 0 {
        out.push(padc);
        padn -= 1;
    }
    each_rev(digits) { out.push(it); };
}

fn format_int(out: u8[>..]&, v: i64, base: i64, width: i64, padc: u8) {
    if v < 0 {
        out.push('-');
        format_uint(out, (0 - v) as u64, base, width - 1, padc);
    } else {
        format_uint(out, v as u64, base, width, padc);
    }
}

// ULEB128 (§3.6), the encoding an image's framing prefix uses
// (docs/design/serialization.md): what to_bytes writes in front of an
// array's bytes, so that a reader of a stream or a file can size its read
// before it has the whole image.
fn format_uleb(out: u8[>..]&, v: i64) {
    var x = v as u64;
    loop {
        let b = (x & 0x7f) as u8;
        x >>= 7;
        if x == 0 { out.push(b); return; }
        out.push(b | 0x80);
    }
}

// The ULEB128 at the front of s: its value and its byte count, or 0, 0 when
// the bytes do not hold one (unterminated, or past the 64-bit range).
fn parse_uleb(s: u8[:]) -> i64, i64 {
    var v = 0 as u64;
    var shift = 0;
    var i = 0;
    while i < s.len && i < 10 {
        let b = s[i];
        if shift > 63 || (shift == 63 && (b & 0x7e) != 0) { return 0, 0; }
        v |= (b & 0x7f) as u64 << shift;
        i++;
        if b < 0x80 { return v as i64, i; }
        shift += 7;
    }
    return 0, 0;
}

// v with a fixed number of decimals, rounded half up; infinities and NaN
// in their text form (§3.7).
fn format_flt(out: u8[>..]&, v: f64, decimals: i64) {
    if v != v || v - v != 0.0 {
        format(out, v);
        return;
    }
    var x = v;
    if x < 0.0 {
        out.push('-');
        x = -x;
    }
    var scale = 1.0;
    for i in decimals { scale *= 10.0; }
    // x is not negative, so truncation is floor and adding 0.5 first rounds
    // half up: `as!`, since dropping the fraction is the point.
    let scaled = (x * scale + 0.5) as! i64;
    let iscale = scale as i64;
    format_uint(out, (scaled / iscale) as u64, 10, 0, ' ');
    if decimals > 0 {
        out.push('.');
        format_uint(out, (scaled % iscale) as u64, 10, decimals, '0');
    }
}

// An optionally signed whole-string integer; false on anything else.
fn parse_int(s: u8[:], base: i64 = 10) -> i64, bool {
    var i = 0;
    var neg = false;
    if s.len > 0 && (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
    }
    if i >= s.len { return 0, false; }
    var v = 0;
    while i < s.len {
        let c = s[i] as i64;
        var d = -1;
        if c >= '0' && c <= '9' { d = c - '0'; }
        else if c >= 'a' && c <= 'z' { d = c - 'a' + 10; }
        else if c >= 'A' && c <= 'Z' { d = c - 'A' + 10; }
        if d < 0 || d >= base { return 0, false; }
        v = v * base + d;
        i += 1;
    }
    if neg { v = -v; }
    return v, true;
}

// 10^0 .. 10^22: every one exact in binary64, which is what makes one
// multiply or divide by it correctly rounded.
let parse_flt_pow10: f64[23] = [1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22];

// A decimal float with optional sign, fraction and exponent; false otherwise.
fn parse_flt(s: u8[:]) -> f64, bool {
    var i = 0;
    var neg = false;
    if s.len > 0 && (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
    }
    // The significant digits as one integer (the first 19 fit a u64), with
    // the decimal exponent adjusted for digits after the point and for any
    // dropped beyond the 19th, so the value is mant * 10^exp10.
    var mant: u64 = 0;
    var mdigits = 0;
    var exp10 = 0;
    var digits = 0;
    while i < s.len && s[i] >= '0' && s[i] <= '9' {
        if mdigits < 19 { mant = mant * 10 + (s[i] - '0'); mdigits += 1; }
        else { exp10 += 1; }
        digits += 1;
        i += 1;
    }
    if i < s.len && s[i] == '.' {
        i += 1;
        while i < s.len && s[i] >= '0' && s[i] <= '9' {
            if mdigits < 19 { mant = mant * 10 + (s[i] - '0'); mdigits += 1; exp10 -= 1; }
            digits += 1;
            i += 1;
        }
    }
    if digits == 0 { return 0.0, false; }
    if i < s.len && (s[i] == 'e' || s[i] == 'E') {
        i += 1;
        var eneg = false;
        if i < s.len && (s[i] == '-' || s[i] == '+') {
            eneg = s[i] == '-';
            i += 1;
        }
        var e = 0;
        var edigits = 0;
        while i < s.len && s[i] >= '0' && s[i] <= '9' {
            if e < 100000 { e = e * 10 + (s[i] - '0'); }
            edigits += 1;
            i += 1;
        }
        if edigits == 0 { return 0.0, false; }
        exp10 += if eneg { -e } else { e };
    }
    // A mantissa below 2^53 converts exactly and a power of ten up to 10^22
    // is exact in binary64, so one multiply or divide is correctly rounded
    // (Clinger's fast path); longer inputs and larger exponents scale in
    // steps and may be off by an ulp. A longer mantissa rounds as it
    // converts.
    var v: f64 = mant;
    var e = exp10;
    while e > 22 { v *= 1e22; e -= 22; }
    while e < -22 { v /= 1e22; e += 22; }
    if e > 0 { v *= parse_flt_pow10[e]; } else if e < 0 { v /= parse_flt_pow10[-e]; }
    if i != s.len { return 0.0, false; }
    if neg { v = -v; }
    return v, true;
}

// Bytewise lexicographic: -1, 0, 1.
fn compare(a: u8[:], b: u8[:]) -> i64 {
    let n = min(a.len, b.len);
    for i in n {
        if a[i] < b[i] { return -1; }
        if b[i] < a[i] { return 1; }
    }
    if a.len < b.len { return -1; }
    if b.len < a.len { return 1; }
    return 0;
}

fn is_space(c: u8) -> bool { c == ' ' || c == '\t' || c == '\n' || c == '\r' }

// ASCII whitespace trimmed: a sub-slice of s, no copy.
fn trim_start(s: u8[:]) -> u8[:] {
    var i = 0;
    while i < s.len && is_space(s[i]) { i += 1; }
    return s[i..];
}
fn trim_end(s: u8[:]) -> u8[:] {
    var e = s.len;
    while e > 0 && is_space(s[e - 1]) { e -= 1; }
    return s[..e];
}
fn trim(s: u8[:]) -> u8[:] { trim_end(trim_start(s)) }

// The parts between separators, as slices into s; empty parts are kept. The
// parts are read-only views, so the same function serves a literal and a
// buffer alike (§9.5).
fn split(s: u8[:], sep: u8) -> const u8[:][>..] {
    var out: const u8[:][>..] = [];
    var start = 0;
    for c, i in s {
        if c == sep {
            out.push(s[start..i]);
            start = i + 1;
        }
    }
    out.push(s[start..]);
    return out;
}
// A separator of several bytes, matched from the left, so occurrences never
// overlap; an empty one leaves s whole.
fn split(s: u8[:], sep: u8[:]) -> const u8[:][>..] {
    var out: const u8[:][>..] = [];
    if sep.len == 0 {
        out.push(s);
        return out;
    }
    var start = 0;
    var i = 0;
    while i + sep.len <= s.len {
        if s[i..i + sep.len] == sep {
            out.push(s[start..i]);
            i += sep.len;
            start = i;
        } else {
            i += 1;
        }
    }
    out.push(s[start..]);
    return out;
}

// F(part) for each part, without building the array.
fn each_split<F>(s: u8[:], sep: u8) {
    var start = 0;
    for c, i in s {
        if c == sep {
            F(s[start..i]);
            start = i + 1;
        }
    }
    F(s[start..]);
}
fn each_split<F>(s: u8[:], sep: u8[:]) {
    if sep.len == 0 {
        F(s);
        return;
    }
    var start = 0;
    var i = 0;
    while i + sep.len <= s.len {
        if s[i..i + sep.len] == sep {
            F(s[start..i]);
            i += sep.len;
            start = i;
        } else {
            i += 1;
        }
    }
    F(s[start..]);
}

// The words of s, as slices into it: the runs of bytes between ASCII
// whitespace (is_space, as trim), so no word is empty.
fn words(s: u8[:]) -> const u8[:][>..] {
    var out: const u8[:][>..] = [];
    var i = 0;
    while i < s.len {
        while i < s.len && is_space(s[i]) { i += 1; }
        let start = i;
        while i < s.len && !is_space(s[i]) { i += 1; }
        if i > start { out.push(s[start..i]); }
    }
    return out;
}

// F(word) for each word, without building the array.
fn each_word<F>(s: u8[:]) {
    var i = 0;
    while i < s.len {
        while i < s.len && is_space(s[i]) { i += 1; }
        let start = i;
        while i < s.len && !is_space(s[i]) { i += 1; }
        if i > start { F(s[start..i]); }
    }
}

// The parts with sep between them, appended to out; T any u8 array or slice type.
fn join<T>(out: u8[>..]&, parts: T[:], sep: u8[:]) {
    for p, i in parts {
        if i > 0 { out.append(sep); }
        out.append(p);
    }
}

// s with every occurrence of `old` replaced by `with`, appended to out.
fn format_replaced(out: u8[>..]&, s: u8[:], old: u8[:], with: u8[:]) {
    if old.len == 0 {
        out.append(s);
        return;
    }
    var start = 0;
    var i = 0;
    while i + old.len <= s.len {
        if s[i..i + old.len] == old {
            out.append(s[start..i]);
            out.append(with);
            i += old.len;
            start = i;
        } else {
            i += 1;
        }
    }
    out.append(s[start..]);
}

// F(codepoint) for each UTF-8 sequence; malformed bytes yield 0xFFFD.
fn each_utf8<F>(s: u8[:]) {
    var i = 0;
    while i < s.len {
        let b = s[i] as i64;
        var cp = 0xFFFD;
        var n = 1;
        if b < 0x80 {
            cp = b;
        } else if b >= 0xC0 && b < 0xE0 && i + 1 < s.len {
            cp = ((b & 0x1F) << 6) | ((s[i + 1] as i64) & 0x3F);
            n = 2;
        } else if b >= 0xE0 && b < 0xF0 && i + 2 < s.len {
            cp = ((b & 0x0F) << 12) | (((s[i + 1] as i64) & 0x3F) << 6) |
                 ((s[i + 2] as i64) & 0x3F);
            n = 3;
        } else if b >= 0xF0 && b < 0xF8 && i + 3 < s.len {
            cp = ((b & 0x07) << 18) | (((s[i + 1] as i64) & 0x3F) << 12) |
                 (((s[i + 2] as i64) & 0x3F) << 6) | ((s[i + 3] as i64) & 0x3F);
            n = 4;
        }
        F(cp);
        i += n;
    }
}

// The UTF-8 bytes of a code point.
fn push_utf8(out: u8[>..]&, cp: i64) {
    if cp < 0x80 {
        out.push(cp as u8);
    } else if cp < 0x800 {
        out.push((0xC0 | (cp >> 6)) as u8);
        out.push((0x80 | (cp & 0x3F)) as u8);
    } else if cp < 0x10000 {
        out.push((0xE0 | (cp >> 12)) as u8);
        out.push((0x80 | ((cp >> 6) & 0x3F)) as u8);
        out.push((0x80 | (cp & 0x3F)) as u8);
    } else {
        out.push((0xF0 | (cp >> 18)) as u8);
        out.push((0x80 | ((cp >> 12) & 0x3F)) as u8);
        out.push((0x80 | ((cp >> 6) & 0x3F)) as u8);
        out.push((0x80 | (cp & 0x3F)) as u8);
    }
}
