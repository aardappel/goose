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

// Truncating remainder pairs with `/`. The product stays between a and
// zero; divisor -1 has remainder zero even when its quotient cannot fit.
fn rem<T>(a: T, b: T) -> T {
    // A negative all-ones divisor is -1; this also accepts unsigned types.
    if b < 0 && ~b == 0 { return 0; }
    return a - (a / b) * b;
}

// Euclidean division pairs with `%`. Adjust the truncating quotient from
// its remainder, without forming a - (a % b), which can underflow.
fn div_euclid<T>(a: T, b: T) -> T {
    let q = a / b;
    if rem(a, b) < 0 {
        if b > 0 { return q - 1; }
        return q + 1;
    }
    return q;
}

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

// The bits set, and the leading / trailing zero bits, 64 for zero: the
// runtime's (src/runtime/runtime_ext.h), one instruction where it can.
extern fn popcount(x: u64) -> i64 = "gs_popcount";
extern fn clz(x: u64) -> i64 = "gs_clz";
extern fn ctz(x: u64) -> i64 = "gs_ctz";

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
// mix (the low bits are what `dictionary` masks and keeps as slot tags). A
// negative integer mixes as its two's-complement bits, and a float as its
// count of 1/65536ths, truncated and wrapped to 64 bits: `as!`, since those
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

// The 8 bytes of s from i, and the 4 bytes, little-endian; C compilers turn
// each into one load.
fn hash_le64(s: u8[:], i: i64) -> u64 {
    let c = s[i..i + 8];
    c[0] | (c[1] as u64) << 8 | (c[2] as u64) << 16 | (c[3] as u64) << 24 |
        (c[4] as u64) << 32 | (c[5] as u64) << 40 | (c[6] as u64) << 48 | (c[7] as u64) << 56
}
fn hash_le32(s: u8[:], i: i64) -> u64 {
    let c = s[i..i + 4];
    c[0] | (c[1] as u64) << 8 | (c[2] as u64) << 16 | (c[3] as u64) << 24
}

// Bytes, 8 at a time: one multiply and a rotate per word but the last,
// which is the final 8 bytes (overlapping the words before) or, for a short
// s, its bytes gathered into one word, and the length is the seed. The last
// word goes through two multiply-xorshift rounds, after which every input bit
// reaches the low bits. Keys longer than 24 bytes take hash_lanes, a call of
// its own from this early return, so that the short keys' code stays small
// enough for the C compiler to inline into a dictionary's lookups.
fn hash(s: u8[:]) -> u64 {
    let n = s.len;
    if n > 24 { return hash_lanes(s); }
    var h = (n as! u64) * 11400714819323198485;
    var w: u64 = 0;
    if n > 8 {
        var i = 0;
        while i < n - 8 {
            h = (h ^ hash_le64(s, i)) * 11400714819323198485;
            h = (h << 29) | (h >> 35);
            i += 8;
        }
        w = hash_le64(s, n - 8);
    } else if n >= 4 {
        w = hash_le32(s, 0) | hash_le32(s, n - 4) << 32;
    } else if n > 0 {
        w = s[0] | (s[n >> 1] as u64) << 8 | (s[n - 1] as u64) << 16;
    }
    h = (h ^ w) * 11400714819323198485;
    h = (h ^ (h >> 32)) * 15485907386658061715;
    return h ^ (h >> 32);
}

// hash of an s longer than 24 bytes. Each word's round waits for the one
// before, so the words before the last 24 bytes or fewer go to two lanes of
// rounds, the even and the odd words of each 16 bytes, which a processor
// overlaps; the second is folded into the first for the remaining words.
fn hash_lanes(s: u8[:]) -> u64 {
    let n = s.len;
    var h = (n as! u64) * 11400714819323198485;
    var g = h ^ 0x9E3779B97F4A7C15;
    var i = 0;
    while i < n - 24 {
        h = (h ^ hash_le64(s, i)) * 11400714819323198485;
        h = (h << 29) | (h >> 35);
        g = (g ^ hash_le64(s, i + 8)) * 11400714819323198485;
        g = (g << 29) | (g >> 35);
        i += 16;
    }
    h = h ^ ((g << 31) | (g >> 33));
    while i < n - 8 {
        h = (h ^ hash_le64(s, i)) * 11400714819323198485;
        h = (h << 29) | (h >> 35);
        i += 8;
    }
    h = (h ^ hash_le64(s, n - 8)) * 11400714819323198485;
    h = (h ^ (h >> 32)) * 15485907386658061715;
    return h ^ (h >> 32);
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
// (spec §3.8), and hold one in a `T` local, which keeps a reference: the
// plain one, for relative elements, which the store back encodes (§3.9).
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
        if xs[i] == sub[0] && xs[i..i + sub.len] == sub { return i; }
        i += 1;
    }
    return -1;
}
fn rfind<T>(xs: T[:], sub: T[:]) -> i64 {
    if sub.len == 0 { return xs.len; }
    if sub.len > xs.len { return -1; }
    var i = xs.len - sub.len;
    while i >= 0 {
        if xs[i] == sub[0] && xs[i..i + sub.len] == sub { return i; }
        i -= 1;
    }
    return -1;
}

fn starts_with<T>(xs: T[:], p: T[:]) -> bool { p.len <= xs.len && xs[..p.len] == p }
fn ends_with<T>(xs: T[:], p: T[:]) -> bool { p.len <= xs.len && xs[xs.len - p.len..] == p }

// ---- byte sets ------------------------------------------------------------------

// A set of bytes, prepared by byte_set for find_any and find_pair, which the
// runtime runs over 16 bytes at a time where it can (gs_scan_any in
// src/runtime/runtime_impl.h, whose gs_byteset has this layout). `members`
// is the set. The rest says how a block is tested for it: kind 1 to 3 by
// comparing with that many of `bytes`, kind 4 as the range bytes[0] ..
// bytes[0] + bytes[1], kind 0 being the empty set, and with `invert` the set
// is the complement of what the kind describes. Every set also has tables,
// bit k of lo[j] standing for the byte k * 16 + j and of hi[j] for 128 + k *
// 16 + j, which are all a set of kind 5 is tested by.
struct ByteSet {
    members: bool[256],
    kind: u8,
    invert: bool,
    bytes: u8[3],
    lo: u8[16],
    hi: u8[16],
}

fn byte_set(members: bool[256]) -> ByteSet {
    var n = 0;
    for m in members { if m { n += 1; } }
    var s = ByteSet { members: members, kind: 5, invert: false, bytes: [0; 3], lo: [0; 16],
                      hi: [0; 16] };
    for b in 256 {
        if members[b] {
            let bit = (1 << ((b >> 4) & 7)) as u8;
            if b < 128 { s.lo[b & 15] |= bit; } else { s.hi[b & 15] |= bit; }
        }
    }
    // The bytes a kind describes: the members, or the others where those
    // are fewer.
    let invert = n > 128;
    var k = 0;
    var first = 0;
    var last = 0;
    for b in 256 {
        if members[b] != invert {
            if k == 0 { first = b; }
            if k < 3 { s.bytes[k] = b as u8; }
            last = b;
            k += 1;
        }
    }
    if k <= 3 {
        s.kind = k as u8;
        s.invert = invert;
        for j in k..3 { s.bytes[j] = s.bytes[0]; }
    } else if last - first + 1 == k {
        s.kind = 4;
        s.invert = invert;
        s.bytes[0] = first as u8;
        s.bytes[1] = (last - first) as u8;
    }
    return s;
}

// The set of the bytes in `bytes`.
fn byte_set(bytes: u8[:]) -> ByteSet {
    var members: bool[256] = [false; 256];
    for b in bytes { members[b] = true; }
    return byte_set(members);
}

// The first i with s[i] in the set, -1 if none.
extern fn find_any(s: const u8[:], set: const ByteSet&) -> i64 = "gs_find_any";

// find_pair, but for its check of d.
extern fn byte_set_pair(s: const u8[:], a: const ByteSet&, d: i64,
                          b: const ByteSet&) -> i64 = "gs_find_pair";

// The first i with s[i] in a and s[i + d] in b, of those with i + d < s.len;
// -1 if none. d >= 0.
fn find_pair(s: u8[:], a: const ByteSet&, d: i64, b: const ByteSet&) -> i64 {
    assert(d >= 0);
    return byte_set_pair(s, a, d, b);
}

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
// stack, so that no pass copies back. The first pass merges out of the
// temporary where the number of passes is odd, so that the last one lands
// in xs. Input that is one run takes the first pass alone, and 20 or fewer
// elements are insertion sorted; neither allocates.
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
    var passes = 0;
    var runs = ends.len;
    while runs > 1 {
        runs = (runs + 1) / 2;
        passes += 1;
    }
    var into_tmp = passes % 2 == 0;
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

// Digits of v in the base (2..36), right-aligned in `width` with `pad`,
// appended to `out`, any u8 array that grows (u8[>..], or a limited u8[..k]).
// Base ten is the builtin text of the integer, the runtime's digit loop
// writing straight into `out`; other bases divide by the `base` parameter,
// a real division per digit.
fn format_uint<B>(out: B&, v: u64, base: i64, width: i64, padc: u8) {
    if base == 10 {
        if width > 1 {
            var n = 1;
            var x = v;
            while x >= 10 {
                x /= 10;
                n += 1;
            }
            var padn = width - n;
            while padn > 0 {
                out.push(padc);
                padn -= 1;
            }
        }
        format(out, v);
        return;
    }
    var digits: u8[..64] = [];
    var x = v;
    if x == 0 { digits.push('0'); }
    let b = base as u64;
    while x > 0 {
        let d = (x % b) as i64;
        var c: u8 = 0;
        if d < 10 { c = ('0' + d) as u8; } else { c = ('a' + d - 10) as u8; }
        digits.push(c);
        x = x / b;
    }
    var padn = width - digits.len;
    while padn > 0 {
        out.push(padc);
        padn -= 1;
    }
    each_rev(digits) { out.push(it); };
}

// The sign goes in front of any padding: -0042. The magnitude is taken in
// unsigned arithmetic, which holds i64.min's.
fn format_int<B>(out: B&, v: i64, base: i64, width: i64, padc: u8) {
    if v < 0 {
        out.push('-');
        format_uint(out, 0 - v as! u64, base, width - 1, padc);
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

// An optionally signed whole-string integer; false on anything else, including a value outside i64.
fn parse_int(s: u8[:], base: i64 = 10) -> i64, bool {
    var i = 0;
    var neg = false;
    if s.len > 0 && (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
    }
    if i >= s.len { return 0, false; }
    // The magnitude accumulates as a negative number: -2^63 has no positive
    // counterpart, while every other magnitude has a negative one. `d < base`
    // is checked first, so `base` is positive in the overflow test; dividing
    // truncates toward zero, which is the ceiling for these negative bounds.
    let min = -9223372036854775807 - 1;
    var v = 0;
    while i < s.len {
        let c = s[i] as i64;
        var d = -1;
        if c >= '0' && c <= '9' { d = c - '0'; }
        else if c >= 'a' && c <= 'z' { d = c - 'a' + 10; }
        else if c >= 'A' && c <= 'Z' { d = c - 'A' + 10; }
        if d < 0 || d >= base { return 0, false; }
        if v < (min + d) / base { return 0, false; }
        v = v * base - d;
        i += 1;
    }
    if !neg {
        if v == min { return 0, false; }
        v = -v;
    }
    return v, true;
}

// A decimal float with optional sign, fraction and exponent, correctly
// rounded; false otherwise.
fn parse_flt(s: u8[:]) -> f64, bool {
    var i = 0;
    var neg = false;
    if s.len > 0 && (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
    }
    // The significant digits as one integer, with the decimal exponent
    // adjusted for the digits after the point, so the value is
    // mant * 10^exp10. One test per digit (the u8 subtraction wraps below
    // '0'), and the digits are counted from positions.
    let start = i;
    var mant: u64 = 0;
    while i < s.len {
        let d = s[i] - '0';
        if d >= 10 { break; }
        mant = mant * 10 + d;
        i += 1;
    }
    let whole = mant;
    var digits = i - start;
    var frac_digits = 0;
    if i < s.len && s[i] == '.' {
        i += 1;
        let fstart = i;
        while i < s.len {
            let d = s[i] - '0';
            if d >= 10 { break; }
            mant = mant * 10 + d;
            i += 1;
        }
        frac_digits = i - fstart;
        digits += frac_digits;
    }
    if digits == 0 { return 0.0, false; }
    var exp10 = -frac_digits;
    // Up to 19 digits fit a u64 exactly. With no exponent either, the value
    // is that integer over a power of ten. Up to 2^53 the integer converts
    // exactly, so the division by an exact power of ten is the one rounding,
    // and with no fraction the conversion is. Converting as an i64 takes one
    // instruction where a u64 takes several on x86-64, and TinyCC's u64
    // conversion can be an ulp off from 2^63 on: larger integers go on to
    // quick.
    if digits <= 19 && i == s.len && mant >> 63 == 0
       && (frac_digits == 0 || mant <= float_text::MAX_EXACT) {
        var v: f64 = mant as! i64;
        if exp10 < 0 { v /= float_text::POW10[-exp10]; }
        if neg { v = -v; }
        return v, true;
    }
    let end = i;
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
    if i != s.len { return 0.0, false; }
    // What quick leaves open, and more than 19 digits, on which mant
    // wrapped, nearest settles from the digits themselves.
    var v = 0.0;
    var settled = false;
    if digits <= 19 { v, settled = float_text::quick(mant, exp10, whole, frac_digits, end == s.len); }
    guard settled else {
        let m, taken, dropped = float_text::significant(s, start, end);
        let x = float_text::nearest(s, start, end, exp10, m, taken, dropped);
        return if neg { -x } else { x }, true;
    }
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

// F(codepoint) for each UTF-8 sequence. Malformed bytes yield 0xFFFD, one
// for each maximal subpart, as Unicode (§3.9) and the WHATWG decoder count
// them: a lead byte and the continuation bytes that fit it, up to the first
// that does not, which then starts the next sequence.
fn each_utf8<F>(s: u8[:]) {
    var i = 0;
    while i < s.len {
        let b = s[i] as i64;
        if b < 0x80 {
            F(b);
            i += 1;
            continue;
        }
        // The length the lead byte announces, its payload bits, and the
        // range of its second byte, which rules out overlong forms,
        // surrogates and code points past 0x10FFFF (Unicode Table 3-7). A
        // byte that cannot lead has n = 0 and is malformed alone.
        var n = 0;
        var cp = 0;
        var lo = 0x80;
        var hi = 0xBF;
        if b >= 0xC2 && b <= 0xDF {
            n = 2;
            cp = b & 0x1F;
        } else if b >= 0xE0 && b <= 0xEF {
            n = 3;
            cp = b & 0x0F;
            if b == 0xE0 { lo = 0xA0; } else if b == 0xED { hi = 0x9F; }
        } else if b >= 0xF0 && b <= 0xF4 {
            n = 4;
            cp = b & 0x07;
            if b == 0xF0 { lo = 0x90; } else if b == 0xF4 { hi = 0x8F; }
        }
        var k = 1;
        while k < n && i + k < s.len {
            let c = s[i + k] as i64;
            if c < lo || c > hi { break; }
            cp = (cp << 6) | (c & 0x3F);
            lo = 0x80;
            hi = 0xBF;
            k += 1;
        }
        if k == n { F(cp); } else { F(0xFFFD); }
        i += k;
    }
}

// The UTF-8 bytes of a code point; 0xFFFD's for one below zero or past
// 0x10FFFF. A surrogate is written as the three bytes of its own code,
// which json keeps a lone \u escape as.
fn push_utf8(out: u8[>..]&, cp: i64) {
    var c = cp;
    if c < 0 || c > 0x10FFFF { c = 0xFFFD; }
    if c < 0x80 {
        out.push(c as u8);
    } else if c < 0x800 {
        out.push((0xC0 | (c >> 6)) as u8);
        out.push((0x80 | (c & 0x3F)) as u8);
    } else if c < 0x10000 {
        out.push((0xE0 | (c >> 12)) as u8);
        out.push((0x80 | ((c >> 6) & 0x3F)) as u8);
        out.push((0x80 | (c & 0x3F)) as u8);
    } else {
        out.push((0xF0 | (c >> 18)) as u8);
        out.push((0x80 | ((c >> 12) & 0x3F)) as u8);
        out.push((0x80 | ((c >> 6) & 0x3F)) as u8);
        out.push((0x80 | (c & 0x3F)) as u8);
    }
}

// ---- float text ------------------------------------------------------------------

// What parse_flt and json read decimal floats with, and json writes them
// with: powers of ten, 128-bit products, and the double nearest to a
// decimal, found exactly. They are declared into a namespace of their own,
// `float_text`, which keeps their names out of the global one.

// 10^0 .. 10^22, each exact in binary64. The tables are `const`, static C
// data rather than members of each program instance's globals.
const float_text::POW10: f64[23] = [1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21,
                                    1e22];

// The powers of ten and of five that fit a u64.
const float_text::POW10_U64: u64[20] = [1, 10, 100, 1000, 10000, 100000, 1000000, 10000000,
                                        100000000, 1000000000, 10000000000, 100000000000,
                                        1000000000000, 10000000000000, 100000000000000,
                                        1000000000000000, 10000000000000000, 100000000000000000,
                                        1000000000000000000, 10000000000000000000];
const float_text::POW5: u64[28] = [1, 5, 25, 125, 625, 3125, 15625, 78125, 390625, 1953125, 9765625,
                                   48828125, 244140625, 1220703125, 6103515625, 30517578125,
                                   152587890625, 762939453125, 3814697265625, 19073486328125,
                                   95367431640625, 476837158203125, 2384185791015625,
                                   11920928955078125, 59604644775390625, 298023223876953125,
                                   1490116119384765625, 7450580596923828125];

// The bits of a double.
fn float_text::bits_of(x: f64) -> u64 {
    let a: f64[1] = [x];
    let b = bytes_of(a);
    return b[0] | (b[1] as u64) << 8 | (b[2] as u64) << 16 | (b[3] as u64) << 24 |
        (b[4] as u64) << 32 | (b[5] as u64) << 40 | (b[6] as u64) << 48 | (b[7] as u64) << 56;
}

// a * b in 128 bits: the high and the low half.
fn float_text::mul_wide(a: u64, b: u64) -> u64, u64 {
    let a0 = a & 0xFFFFFFFF;
    let a1 = a >> 32;
    let b0 = b & 0xFFFFFFFF;
    let b1 = b >> 32;
    let p00 = a0 * b0;
    let p01 = a0 * b1;
    let p10 = a1 * b0;
    let mid = (p00 >> 32) + (p01 & 0xFFFFFFFF) + (p10 & 0xFFFFFFFF);
    return a1 * b1 + (p01 >> 32) + (p10 >> 32) + (mid >> 32), (mid << 32) | (p00 & 0xFFFFFFFF);
}

// 2^53: the integers up to it are exact in binary64.
let float_text::MAX_EXACT: u64 = 9007199254740992;

// v * 10^e, in steps of an exact power of ten.
fn float_text::scale(v: f64, e: i64) -> f64 {
    var x = v;
    var k = e;
    while k > 22 { x *= 1e22; k -= 22; }
    while k < -22 { x /= 1e22; k += 22; }
    if k > 0 { x *= POW10[k]; } else if k < 0 { x /= POW10[-k]; }
    return x;
}

// -- reading

// The double nearest to mant * 10^exp10, mant of at most 19 digits, and
// whether that settles it; `whole` is the integer of the digits before a
// point, frac_digits how many follow it, and `plain` that no exponent
// does. Where mant is at most 2^53 and the exponent at most 22 either way,
// that integer and the power of ten that scales it are both exact, so the
// one multiply or divide is correctly rounded (Clinger's fast path). Where
// mant is more, as 17 significant digits make, a plain decimal whose whole
// part w and fraction's digits f make at most 2^53 each is w + f / 10^k:
// f / 10^k is rounded once, and adding w rounds again, which gives the
// correctly rounded sum unless the sum lies within an ulp of f / 10^k of a
// point halfway between two doubles. Rounding is monotonic, so if the sums
// with f / 10^k nudged an ulp down and up round alike, that is the answer.
// Any other number goes to eisel_lemire.
inline fn float_text::quick(mant: u64, exp10: i64, whole: u64, frac_digits: i64,
                            plain: bool) -> f64, bool {
    var v: f64 = mant;
    if mant <= MAX_EXACT && exp10 >= -22 && exp10 <= 22 {
        if exp10 > 0 { v *= POW10[exp10]; } else if exp10 < 0 { v /= POW10[-exp10]; }
        return v, true;
    }
    let frac = mant - whole * POW10_U64[frac_digits];
    if plain && frac_digits > 0 && whole <= MAX_EXACT && frac <= MAX_EXACT {
        let w: f64 = whole;
        let f: f64 = frac;
        let q = f / POW10[frac_digits];
        v = w + q * 0.9999999999999998;
        return v, v == w + q * 1.0000000000000002;
    }
    let x, ok = eisel_lemire(mant, exp10);
    return x, ok;
}

// The first 19 significant digits of text[from..to], decimal digits with
// perhaps a point among them, as an integer, how many digits those are,
// and how many significant digits follow them.
inline fn float_text::significant(text: u8[:], from: i64, to: i64) -> u64, i64, i64 {
    var mant: u64 = 0;
    var taken = 0;
    var dropped = 0;
    for c in text[from..to] {
        if c == '.' || (taken == 0 && c == '0') { continue; }
        if taken < 19 { mant = mant * 10 + (c - '0'); taken++; } else { dropped++; }
    }
    return mant, taken, dropped;
}

// The double nearest to D * 10^e10, D the digits text[from..to] (a point
// among them is passed over), of which `significant` gave mant, taken and
// dropped. Where mant holds all of D, and it and the power of ten that
// scales it are both exact, one multiply or divide is correctly rounded.
// Otherwise the value they give in steps is within a few ulps, and
// round_near, for a D all in mant and a power of ten up to 10^27 either
// way, eisel_lemire for one of those beyond that, or round_exact settles it.
fn float_text::nearest(text: u8[:], from: i64, to: i64, e10: i64, mant: u64, taken: i64,
                       dropped: i64) -> f64 {
    // A value other than zero is below 10^(digits + e10) and at least a
    // tenth of that.
    let digits = taken + dropped;
    let v: f64 = mant;
    if digits == 0 || digits + e10 < -324 { return 0.0; }
    if digits + e10 > 310 { return 1.0 / 0.0; }
    if dropped == 0 && mant <= MAX_EXACT && e10 >= -22 && e10 <= 22 { return scale(v, e10); }
    if dropped == 0 && e10 >= -27 && e10 <= 27 { return round_near(mant, e10, scale(v, e10)); }
    if dropped == 0 {
        let w, ok = eisel_lemire(mant, e10);
        if ok { return w; }
    }
    return round_exact(text, from, to, e10, scale(v, e10 + dropped));
}

// -- fast rounding

// The double nearest to mant * 10^exp10, by the Eisel-Lemire algorithm
// (Lemire, "Number Parsing at a Gigabyte per Second", 2021), as Go's strconv
// has it: mant, shifted to fill 64 bits, times the leading 64 bits of 10^exp10
// gives the double's 54 leading bits, unless the product's low bits are so
// near a carry that the 64 bits of 10^exp10 after those could change them;
// then those take part too. False, rarely, where even that leaves the
// rounding open, or the result would be subnormal or infinite: the exact
// path decides those.
fn float_text::eisel_lemire(mant: u64, exp10: i64) -> f64, bool {
    if mant == 0 { return 0.0, true; }
    guard exp10 >= POW10_128_MIN && exp10 < POW10_128_MIN + POW10_128.len / 2 else { return 0.0, false; }
    let lz = clz(mant);
    let m = mant << lz;
    // 217706 / 2^16 is log2(10) closely enough for every exp10 here.
    var exp2 = ((217706 * exp10) >> 16) + 64 + 1023 - lz;
    let row = 2 * (exp10 - POW10_128_MIN);
    var hi, lo = mul_wide(m, POW10_128[row]);
    if hi & 0x1FF == 0x1FF && lo + m < m {
        let yhi, ylo = mul_wide(m, POW10_128[row + 1]);
        var mhi = hi;
        let mlo = lo + yhi;
        if mlo < lo { mhi++; }
        if mhi & 0x1FF == 0x1FF && mlo + 1 == 0 && ylo + m < m { return 0.0, false; }
        hi = mhi;
        lo = mlo;
    }
    // 54 bits, the top one of hi or the one below it first, then rounded to
    // 53, half to even. A product exactly halfway is not settled here.
    let top = hi >> 63;
    var r = hi >> (top + 9);
    if top == 0 { exp2--; }
    if lo == 0 && hi & 0x1FF == 0 && r & 3 == 1 { return 0.0, false; }
    r += r & 1;
    r >>= 1;
    if r >> 53 > 0 {
        r >>= 1;
        exp2++;
    }
    guard exp2 >= 1 && exp2 < 0x7FF else { return 0.0, false; }
    return ldexp(r, (exp2 - 1075) as! i32), true;
}

// -- exact rounding

// The C library's split of a double into a fraction in [0.5, 1) and a power
// of two, and its inverse.
extern fn float_text::frexp(x: f64, e: i32&) -> f64;
extern fn float_text::ldexp(x: f64, e: i32) -> f64;

// The double nearest to D * 10^e10, D the decimal digits text[from..to]
// (a point among them is passed over), given an estimate v within a few
// ulps of it. With v = m * 2^k, v steps an ulp at a time until the exact
// value lies within the halfway points to its neighbours, ties going to the
// even m. Each comparison is exact, in big integers.
fn float_text::round_exact(text: u8[:], from: i64, to: i64, e10: i64, v: f64) -> f64 {
    var digits: u32[>..] = [];
    for c in text[from..to] {
        if c != '.' { big_mul_add(digits, 10, c - '0'); }
    }
    // m below 2^53, k from -1074, the subnormals' exponent, to 971, the
    // largest finite double's.
    var m: u64 = 0;
    var k = -1074;
    if v - v != 0.0 {
        m = MAX_EXACT - 1;
        k = 971;
    } else if v > 0.0 {
        var e: i32 = 0;
        let f = frexp(v, e);
        m = (f * 9007199254740992.0) as! u64;
        k = e - 53;
        if k < -1074 {
            m >>= -1074 - k;
            k = -1074;
        }
    }
    loop {
        let up = compare_exact(digits, e10, 2 * m + 1, k - 1);
        if up > 0 || (up == 0 && m % 2 == 1) {
            m++;
            if m == MAX_EXACT { m = MAX_EXACT / 2; k++; }
            if k > 971 { return 1.0 / 0.0; }
            continue;
        }
        if m == 0 { break; }
        // Below a power of two the neighbour is half as far.
        let down = if m == MAX_EXACT / 2 && k > -1074 { compare_exact(digits, e10, 4 * m - 1, k - 2) }
                   else { compare_exact(digits, e10, 2 * m - 1, k - 1) };
        if down < 0 || (down == 0 && m % 2 == 1) {
            m--;
            if m < MAX_EXACT / 2 && k > -1074 { m = 2 * m + 1; k--; }
            continue;
        }
        break;
    }
    return ldexp(m, k as! i32);
}

// round_exact for w * 10^e10, |e10| <= 27, where the numbers it compares
// fit 128 bits: 5^27 is below 2^63.
fn float_text::round_near(w: u64, e10: i64, v: f64) -> f64 {
    let b = bits_of(v);
    let m0 = (b & 0xFFFFFFFFFFFFF) | 0x10000000000000;
    let be = ((b >> 52) & 0x7FF) as! i64;
    let k0 = be - 1075;
    var m = m0;
    var k = k0;
    loop {
        let up = compare_near(w, e10, 2 * m + 1, k - 1);
        if up > 0 || (up == 0 && m % 2 == 1) {
            m++;
            if m == MAX_EXACT { m = MAX_EXACT / 2; k++; }
            continue;
        }
        let down = if m == MAX_EXACT / 2 { compare_near(w, e10, 4 * m - 1, k - 2) }
                   else { compare_near(w, e10, 2 * m - 1, k - 1) };
        if down < 0 || (down == 0 && m % 2 == 1) {
            m--;
            if m < MAX_EXACT / 2 { m = 2 * m + 1; k--; }
            continue;
        }
        break;
    }
    return if m == m0 && k == k0 { v } else { ldexp(m, k as! i32) };
}

// w * 10^e10 against h * 2^p, as compare_exact. That is w * 5^e10 against
// h * 2^(p - e10), the power of five on whichever side keeps it whole; the
// two are within a few ulps of each other, so the one shifted left to line
// them up stays within 128 bits.
fn float_text::compare_near(w: u64, e10: i64, h: u64, p: i64) -> i64 {
    var ah: u64 = 0;
    var al = w;
    var bh: u64 = 0;
    var bl = h;
    if e10 >= 0 { ah, al = mul_wide(w, POW5[e10]); } else { bh, bl = mul_wide(h, POW5[-e10]); }
    let d = e10 - p;
    if d > 0 { ah, al = shl_wide(ah, al, d); } else if d < 0 { bh, bl = shl_wide(bh, bl, -d); }
    if ah != bh { return if ah < bh { -1 } else { 1 }; }
    if al != bl { return if al < bl { -1 } else { 1 }; }
    return 0;
}

// The 128-bit h * 2^64 + l shifted left by s, 0 < s < 128.
fn float_text::shl_wide(h: u64, l: u64, s: i64) -> u64, u64 {
    if s >= 64 { return l << (s - 64), 0; }
    return (h << s) | (l >> (64 - s)), l << s;
}

// D * 10^e10 against h * 2^p: -1, 0 or 1 as it is below, equal to or above.
fn float_text::compare_exact(digits: u32[:], e10: i64, h: u64, p: i64) -> i64 {
    var a: u32[>..] = [];
    a.append(digits);
    var b: u32[>..] = [];
    b.push(h as! u32);
    if h >> 32 > 0 { b.push((h >> 32) as! u32); }
    if e10 >= 0 { big_mul_pow5(a, e10); } else { big_mul_pow5(b, -e10); }
    if e10 >= p { big_shl(a, e10 - p); } else { big_shl(b, p - e10); }
    return big_cmp(a, b);
}

// Big integers are their base-2^32 limbs, least significant first, with no
// zero limbs on top: zero has none.

// x = x * m + a.
fn float_text::big_mul_add(x: u32[>..]&, m: u32, a: u32) {
    var carry: u64 = a;
    for &limb in x {
        let t = limb as u64 * m + carry;
        limb = t as! u32;
        carry = t >> 32;
    }
    if carry > 0 { x.push(carry as! u32); }
}

// x = x * 5^e.
fn float_text::big_mul_pow5(x: u32[>..]&, e: i64) {
    var k = e;
    while k >= 13 { big_mul_add(x, 1220703125, 0); k -= 13; }
    var m: u32 = 1;
    for i in k { m *= 5; }
    big_mul_add(x, m, 0);
}

// x = x * 2^e.
fn float_text::big_shl(x: u32[>..]&, e: i64) {
    var k = e;
    while k >= 31 { big_mul_add(x, 0x80000000, 0); k -= 31; }
    big_mul_add(x, (1 as u32) << k, 0);
}

fn float_text::big_cmp(a: u32[:], b: u32[:]) -> i64 {
    if a.len != b.len { return if a.len < b.len { -1 } else { 1 }; }
    var i = a.len;
    while i > 0 {
        i--;
        if a[i] != b[i] { return if a[i] < b[i] { -1 } else { 1 }; }
    }
    return 0;
}

// -- powers of ten

// 10^q for q from POW10_128_MIN on, each as its leading 128 bits, rounded down:
// two u64s, the high half first, the top bit set. 10^q is that times 2^(e - 127)
// for the e with 2^e <= 10^q < 2^(e + 1). Printed by scripts/pow10_table.py
// (Go's strconv and Wuffs hold the same table).
let float_text::POW10_128_MIN = -348;

const float_text::POW10_128: u64[1392] = [
    0xFA8FD5A0081C0288, 0x1732C869CD60E453, 0x9C99E58405118195, 0x0E7FBD42205C8EB4,   // 1e-348, 1e-347
    0xC3C05EE50655E1FA, 0x521FAC92A873B261, 0xF4B0769E47EB5A78, 0xE6A797B752909EF9,   // 1e-346, 1e-345
    0x98EE4A22ECF3188B, 0x9028BED2939A635C, 0xBF29DCABA82FDEAE, 0x7432EE873880FC33,   // 1e-344, 1e-343
    0xEEF453D6923BD65A, 0x113FAA2906A13B3F, 0x9558B4661B6565F8, 0x4AC7CA59A424C507,   // 1e-342, 1e-341
    0xBAAEE17FA23EBF76, 0x5D79BCF00D2DF649, 0xE95A99DF8ACE6F53, 0xF4D82C2C107973DC,   // 1e-340, 1e-339
    0x91D8A02BB6C10594, 0x79071B9B8A4BE869, 0xB64EC836A47146F9, 0x9748E2826CDEE284,   // 1e-338, 1e-337
    0xE3E27A444D8D98B7, 0xFD1B1B2308169B25, 0x8E6D8C6AB0787F72, 0xFE30F0F5E50E20F7,   // 1e-336, 1e-335
    0xB208EF855C969F4F, 0xBDBD2D335E51A935, 0xDE8B2B66B3BC4723, 0xAD2C788035E61382,   // 1e-334, 1e-333
    0x8B16FB203055AC76, 0x4C3BCB5021AFCC31, 0xADDCB9E83C6B1793, 0xDF4ABE242A1BBF3D,   // 1e-332, 1e-331
    0xD953E8624B85DD78, 0xD71D6DAD34A2AF0D, 0x87D4713D6F33AA6B, 0x8672648C40E5AD68,   // 1e-330, 1e-329
    0xA9C98D8CCB009506, 0x680EFDAF511F18C2, 0xD43BF0EFFDC0BA48, 0x0212BD1B2566DEF2,   // 1e-328, 1e-327
    0x84A57695FE98746D, 0x014BB630F7604B57, 0xA5CED43B7E3E9188, 0x419EA3BD35385E2D,   // 1e-326, 1e-325
    0xCF42894A5DCE35EA, 0x52064CAC828675B9, 0x818995CE7AA0E1B2, 0x7343EFEBD1940993,   // 1e-324, 1e-323
    0xA1EBFB4219491A1F, 0x1014EBE6C5F90BF8, 0xCA66FA129F9B60A6, 0xD41A26E077774EF6,   // 1e-322, 1e-321
    0xFD00B897478238D0, 0x8920B098955522B4, 0x9E20735E8CB16382, 0x55B46E5F5D5535B0,   // 1e-320, 1e-319
    0xC5A890362FDDBC62, 0xEB2189F734AA831D, 0xF712B443BBD52B7B, 0xA5E9EC7501D523E4,   // 1e-318, 1e-317
    0x9A6BB0AA55653B2D, 0x47B233C92125366E, 0xC1069CD4EABE89F8, 0x999EC0BB696E840A,   // 1e-316, 1e-315
    0xF148440A256E2C76, 0xC00670EA43CA250D, 0x96CD2A865764DBCA, 0x380406926A5E5728,   // 1e-314, 1e-313
    0xBC807527ED3E12BC, 0xC605083704F5ECF2, 0xEBA09271E88D976B, 0xF7864A44C633682E,   // 1e-312, 1e-311
    0x93445B8731587EA3, 0x7AB3EE6AFBE0211D, 0xB8157268FDAE9E4C, 0x5960EA05BAD82964,   // 1e-310, 1e-309
    0xE61ACF033D1A45DF, 0x6FB92487298E33BD, 0x8FD0C16206306BAB, 0xA5D3B6D479F8E056,   // 1e-308, 1e-307
    0xB3C4F1BA87BC8696, 0x8F48A4899877186C, 0xE0B62E2929ABA83C, 0x331ACDABFE94DE87,   // 1e-306, 1e-305
    0x8C71DCD9BA0B4925, 0x9FF0C08B7F1D0B14, 0xAF8E5410288E1B6F, 0x07ECF0AE5EE44DD9,   // 1e-304, 1e-303
    0xDB71E91432B1A24A, 0xC9E82CD9F69D6150, 0x892731AC9FAF056E, 0xBE311C083A225CD2,   // 1e-302, 1e-301
    0xAB70FE17C79AC6CA, 0x6DBD630A48AAF406, 0xD64D3D9DB981787D, 0x092CBBCCDAD5B108,   // 1e-300, 1e-299
    0x85F0468293F0EB4E, 0x25BBF56008C58EA5, 0xA76C582338ED2621, 0xAF2AF2B80AF6F24E,   // 1e-298, 1e-297
    0xD1476E2C07286FAA, 0x1AF5AF660DB4AEE1, 0x82CCA4DB847945CA, 0x50D98D9FC890ED4D,   // 1e-296, 1e-295
    0xA37FCE126597973C, 0xE50FF107BAB528A0, 0xCC5FC196FEFD7D0C, 0x1E53ED49A96272C8,   // 1e-294, 1e-293
    0xFF77B1FCBEBCDC4F, 0x25E8E89C13BB0F7A, 0x9FAACF3DF73609B1, 0x77B191618C54E9AC,   // 1e-292, 1e-291
    0xC795830D75038C1D, 0xD59DF5B9EF6A2417, 0xF97AE3D0D2446F25, 0x4B0573286B44AD1D,   // 1e-290, 1e-289
    0x9BECCE62836AC577, 0x4EE367F9430AEC32, 0xC2E801FB244576D5, 0x229C41F793CDA73F,   // 1e-288, 1e-287
    0xF3A20279ED56D48A, 0x6B43527578C1110F, 0x9845418C345644D6, 0x830A13896B78AAA9,   // 1e-286, 1e-285
    0xBE5691EF416BD60C, 0x23CC986BC656D553, 0xEDEC366B11C6CB8F, 0x2CBFBE86B7EC8AA8,   // 1e-284, 1e-283
    0x94B3A202EB1C3F39, 0x7BF7D71432F3D6A9, 0xB9E08A83A5E34F07, 0xDAF5CCD93FB0CC53,   // 1e-282, 1e-281
    0xE858AD248F5C22C9, 0xD1B3400F8F9CFF68, 0x91376C36D99995BE, 0x23100809B9C21FA1,   // 1e-280, 1e-279
    0xB58547448FFFFB2D, 0xABD40A0C2832A78A, 0xE2E69915B3FFF9F9, 0x16C90C8F323F516C,   // 1e-278, 1e-277
    0x8DD01FAD907FFC3B, 0xAE3DA7D97F6792E3, 0xB1442798F49FFB4A, 0x99CD11CFDF41779C,   // 1e-276, 1e-275
    0xDD95317F31C7FA1D, 0x40405643D711D583, 0x8A7D3EEF7F1CFC52, 0x482835EA666B2572,   // 1e-274, 1e-273
    0xAD1C8EAB5EE43B66, 0xDA3243650005EECF, 0xD863B256369D4A40, 0x90BED43E40076A82,   // 1e-272, 1e-271
    0x873E4F75E2224E68, 0x5A7744A6E804A291, 0xA90DE3535AAAE202, 0x711515D0A205CB36,   // 1e-270, 1e-269
    0xD3515C2831559A83, 0x0D5A5B44CA873E03, 0x8412D9991ED58091, 0xE858790AFE9486C2,   // 1e-268, 1e-267
    0xA5178FFF668AE0B6, 0x626E974DBE39A872, 0xCE5D73FF402D98E3, 0xFB0A3D212DC8128F,   // 1e-266, 1e-265
    0x80FA687F881C7F8E, 0x7CE66634BC9D0B99, 0xA139029F6A239F72, 0x1C1FFFC1EBC44E80,   // 1e-264, 1e-263
    0xC987434744AC874E, 0xA327FFB266B56220, 0xFBE9141915D7A922, 0x4BF1FF9F0062BAA8,   // 1e-262, 1e-261
    0x9D71AC8FADA6C9B5, 0x6F773FC3603DB4A9, 0xC4CE17B399107C22, 0xCB550FB4384D21D3,   // 1e-260, 1e-259
    0xF6019DA07F549B2B, 0x7E2A53A146606A48, 0x99C102844F94E0FB, 0x2EDA7444CBFC426D,   // 1e-258, 1e-257
    0xC0314325637A1939, 0xFA911155FEFB5308, 0xF03D93EEBC589F88, 0x793555AB7EBA27CA,   // 1e-256, 1e-255
    0x96267C7535B763B5, 0x4BC1558B2F3458DE, 0xBBB01B9283253CA2, 0x9EB1AAEDFB016F16,   // 1e-254, 1e-253
    0xEA9C227723EE8BCB, 0x465E15A979C1CADC, 0x92A1958A7675175F, 0x0BFACD89EC191EC9,   // 1e-252, 1e-251
    0xB749FAED14125D36, 0xCEF980EC671F667B, 0xE51C79A85916F484, 0x82B7E12780E7401A,   // 1e-250, 1e-249
    0x8F31CC0937AE58D2, 0xD1B2ECB8B0908810, 0xB2FE3F0B8599EF07, 0x861FA7E6DCB4AA15,   // 1e-248, 1e-247
    0xDFBDCECE67006AC9, 0x67A791E093E1D49A, 0x8BD6A141006042BD, 0xE0C8BB2C5C6D24E0,   // 1e-246, 1e-245
    0xAECC49914078536D, 0x58FAE9F773886E18, 0xDA7F5BF590966848, 0xAF39A475506A899E,   // 1e-244, 1e-243
    0x888F99797A5E012D, 0x6D8406C952429603, 0xAAB37FD7D8F58178, 0xC8E5087BA6D33B83,   // 1e-242, 1e-241
    0xD5605FCDCF32E1D6, 0xFB1E4A9A90880A64, 0x855C3BE0A17FCD26, 0x5CF2EEA09A55067F,   // 1e-240, 1e-239
    0xA6B34AD8C9DFC06F, 0xF42FAA48C0EA481E, 0xD0601D8EFC57B08B, 0xF13B94DAF124DA26,   // 1e-238, 1e-237
    0x823C12795DB6CE57, 0x76C53D08D6B70858, 0xA2CB1717B52481ED, 0x54768C4B0C64CA6E,   // 1e-236, 1e-235
    0xCB7DDCDDA26DA268, 0xA9942F5DCF7DFD09, 0xFE5D54150B090B02, 0xD3F93B35435D7C4C,   // 1e-234, 1e-233
    0x9EFA548D26E5A6E1, 0xC47BC5014A1A6DAF, 0xC6B8E9B0709F109A, 0x359AB6419CA1091B,   // 1e-232, 1e-231
    0xF867241C8CC6D4C0, 0xC30163D203C94B62, 0x9B407691D7FC44F8, 0x79E0DE63425DCF1D,   // 1e-230, 1e-229
    0xC21094364DFB5636, 0x985915FC12F542E4, 0xF294B943E17A2BC4, 0x3E6F5B7B17B2939D,   // 1e-228, 1e-227
    0x979CF3CA6CEC5B5A, 0xA705992CEECF9C42, 0xBD8430BD08277231, 0x50C6FF782A838353,   // 1e-226, 1e-225
    0xECE53CEC4A314EBD, 0xA4F8BF5635246428, 0x940F4613AE5ED136, 0x871B7795E136BE99,   // 1e-224, 1e-223
    0xB913179899F68584, 0x28E2557B59846E3F, 0xE757DD7EC07426E5, 0x331AEADA2FE589CF,   // 1e-222, 1e-221
    0x9096EA6F3848984F, 0x3FF0D2C85DEF7621, 0xB4BCA50B065ABE63, 0x0FED077A756B53A9,   // 1e-220, 1e-219
    0xE1EBCE4DC7F16DFB, 0xD3E8495912C62894, 0x8D3360F09CF6E4BD, 0x64712DD7ABBBD95C,   // 1e-218, 1e-217
    0xB080392CC4349DEC, 0xBD8D794D96AACFB3, 0xDCA04777F541C567, 0xECF0D7A0FC5583A0,   // 1e-216, 1e-215
    0x89E42CAAF9491B60, 0xF41686C49DB57244, 0xAC5D37D5B79B6239, 0x311C2875C522CED5,   // 1e-214, 1e-213
    0xD77485CB25823AC7, 0x7D633293366B828B, 0x86A8D39EF77164BC, 0xAE5DFF9C02033197,   // 1e-212, 1e-211
    0xA8530886B54DBDEB, 0xD9F57F830283FDFC, 0xD267CAA862A12D66, 0xD072DF63C324FD7B,   // 1e-210, 1e-209
    0x8380DEA93DA4BC60, 0x4247CB9E59F71E6D, 0xA46116538D0DEB78, 0x52D9BE85F074E608,   // 1e-208, 1e-207
    0xCD795BE870516656, 0x67902E276C921F8B, 0x806BD9714632DFF6, 0x00BA1CD8A3DB53B6,   // 1e-206, 1e-205
    0xA086CFCD97BF97F3, 0x80E8A40ECCD228A4, 0xC8A883C0FDAF7DF0, 0x6122CD128006B2CD,   // 1e-204, 1e-203
    0xFAD2A4B13D1B5D6C, 0x796B805720085F81, 0x9CC3A6EEC6311A63, 0xCBE3303674053BB0,   // 1e-202, 1e-201
    0xC3F490AA77BD60FC, 0xBEDBFC4411068A9C, 0xF4F1B4D515ACB93B, 0xEE92FB5515482D44,   // 1e-200, 1e-199
    0x991711052D8BF3C5, 0x751BDD152D4D1C4A, 0xBF5CD54678EEF0B6, 0xD262D45A78A0635D,   // 1e-198, 1e-197
    0xEF340A98172AACE4, 0x86FB897116C87C34, 0x9580869F0E7AAC0E, 0xD45D35E6AE3D4DA0,   // 1e-196, 1e-195
    0xBAE0A846D2195712, 0x8974836059CCA109, 0xE998D258869FACD7, 0x2BD1A438703FC94B,   // 1e-194, 1e-193
    0x91FF83775423CC06, 0x7B6306A34627DDCF, 0xB67F6455292CBF08, 0x1A3BC84C17B1D542,   // 1e-192, 1e-191
    0xE41F3D6A7377EECA, 0x20CABA5F1D9E4A93, 0x8E938662882AF53E, 0x547EB47B7282EE9C,   // 1e-190, 1e-189
    0xB23867FB2A35B28D, 0xE99E619A4F23AA43, 0xDEC681F9F4C31F31, 0x6405FA00E2EC94D4,   // 1e-188, 1e-187
    0x8B3C113C38F9F37E, 0xDE83BC408DD3DD04, 0xAE0B158B4738705E, 0x9624AB50B148D445,   // 1e-186, 1e-185
    0xD98DDAEE19068C76, 0x3BADD624DD9B0957, 0x87F8A8D4CFA417C9, 0xE54CA5D70A80E5D6,   // 1e-184, 1e-183
    0xA9F6D30A038D1DBC, 0x5E9FCF4CCD211F4C, 0xD47487CC8470652B, 0x7647C3200069671F,   // 1e-182, 1e-181
    0x84C8D4DFD2C63F3B, 0x29ECD9F40041E073, 0xA5FB0A17C777CF09, 0xF468107100525890,   // 1e-180, 1e-179
    0xCF79CC9DB955C2CC, 0x7182148D4066EEB4, 0x81AC1FE293D599BF, 0xC6F14CD848405530,   // 1e-178, 1e-177
    0xA21727DB38CB002F, 0xB8ADA00E5A506A7C, 0xCA9CF1D206FDC03B, 0xA6D90811F0E4851C,   // 1e-176, 1e-175
    0xFD442E4688BD304A, 0x908F4A166D1DA663, 0x9E4A9CEC15763E2E, 0x9A598E4E043287FE,   // 1e-174, 1e-173
    0xC5DD44271AD3CDBA, 0x40EFF1E1853F29FD, 0xF7549530E188C128, 0xD12BEE59E68EF47C,   // 1e-172, 1e-171
    0x9A94DD3E8CF578B9, 0x82BB74F8301958CE, 0xC13A148E3032D6E7, 0xE36A52363C1FAF01,   // 1e-170, 1e-169
    0xF18899B1BC3F8CA1, 0xDC44E6C3CB279AC1, 0x96F5600F15A7B7E5, 0x29AB103A5EF8C0B9,   // 1e-168, 1e-167
    0xBCB2B812DB11A5DE, 0x7415D448F6B6F0E7, 0xEBDF661791D60F56, 0x111B495B3464AD21,   // 1e-166, 1e-165
    0x936B9FCEBB25C995, 0xCAB10DD900BEEC34, 0xB84687C269EF3BFB, 0x3D5D514F40EEA742,   // 1e-164, 1e-163
    0xE65829B3046B0AFA, 0x0CB4A5A3112A5112, 0x8FF71A0FE2C2E6DC, 0x47F0E785EABA72AB,   // 1e-162, 1e-161
    0xB3F4E093DB73A093, 0x59ED216765690F56, 0xE0F218B8D25088B8, 0x306869C13EC3532C,   // 1e-160, 1e-159
    0x8C974F7383725573, 0x1E414218C73A13FB, 0xAFBD2350644EEACF, 0xE5D1929EF90898FA,   // 1e-158, 1e-157
    0xDBAC6C247D62A583, 0xDF45F746B74ABF39, 0x894BC396CE5DA772, 0x6B8BBA8C328EB783,   // 1e-156, 1e-155
    0xAB9EB47C81F5114F, 0x066EA92F3F326564, 0xD686619BA27255A2, 0xC80A537B0EFEFEBD,   // 1e-154, 1e-153
    0x8613FD0145877585, 0xBD06742CE95F5F36, 0xA798FC4196E952E7, 0x2C48113823B73704,   // 1e-152, 1e-151
    0xD17F3B51FCA3A7A0, 0xF75A15862CA504C5, 0x82EF85133DE648C4, 0x9A984D73DBE722FB,   // 1e-150, 1e-149
    0xA3AB66580D5FDAF5, 0xC13E60D0D2E0EBBA, 0xCC963FEE10B7D1B3, 0x318DF905079926A8,   // 1e-148, 1e-147
    0xFFBBCFE994E5C61F, 0xFDF17746497F7052, 0x9FD561F1FD0F9BD3, 0xFEB6EA8BEDEFA633,   // 1e-146, 1e-145
    0xC7CABA6E7C5382C8, 0xFE64A52EE96B8FC0, 0xF9BD690A1B68637B, 0x3DFDCE7AA3C673B0,   // 1e-144, 1e-143
    0x9C1661A651213E2D, 0x06BEA10CA65C084E, 0xC31BFA0FE5698DB8, 0x486E494FCFF30A62,   // 1e-142, 1e-141
    0xF3E2F893DEC3F126, 0x5A89DBA3C3EFCCFA, 0x986DDB5C6B3A76B7, 0xF89629465A75E01C,   // 1e-140, 1e-139
    0xBE89523386091465, 0xF6BBB397F1135823, 0xEE2BA6C0678B597F, 0x746AA07DED582E2C,   // 1e-138, 1e-137
    0x94DB483840B717EF, 0xA8C2A44EB4571CDC, 0xBA121A4650E4DDEB, 0x92F34D62616CE413,   // 1e-136, 1e-135
    0xE896A0D7E51E1566, 0x77B020BAF9C81D17, 0x915E2486EF32CD60, 0x0ACE1474DC1D122E,   // 1e-134, 1e-133
    0xB5B5ADA8AAFF80B8, 0x0D819992132456BA, 0xE3231912D5BF60E6, 0x10E1FFF697ED6C69,   // 1e-132, 1e-131
    0x8DF5EFABC5979C8F, 0xCA8D3FFA1EF463C1, 0xB1736B96B6FD83B3, 0xBD308FF8A6B17CB2,   // 1e-130, 1e-129
    0xDDD0467C64BCE4A0, 0xAC7CB3F6D05DDBDE, 0x8AA22C0DBEF60EE4, 0x6BCDF07A423AA96B,   // 1e-128, 1e-127
    0xAD4AB7112EB3929D, 0x86C16C98D2C953C6, 0xD89D64D57A607744, 0xE871C7BF077BA8B7,   // 1e-126, 1e-125
    0x87625F056C7C4A8B, 0x11471CD764AD4972, 0xA93AF6C6C79B5D2D, 0xD598E40D3DD89BCF,   // 1e-124, 1e-123
    0xD389B47879823479, 0x4AFF1D108D4EC2C3, 0x843610CB4BF160CB, 0xCEDF722A585139BA,   // 1e-122, 1e-121
    0xA54394FE1EEDB8FE, 0xC2974EB4EE658828, 0xCE947A3DA6A9273E, 0x733D226229FEEA32,   // 1e-120, 1e-119
    0x811CCC668829B887, 0x0806357D5A3F525F, 0xA163FF802A3426A8, 0xCA07C2DCB0CF26F7,   // 1e-118, 1e-117
    0xC9BCFF6034C13052, 0xFC89B393DD02F0B5, 0xFC2C3F3841F17C67, 0xBBAC2078D443ACE2,   // 1e-116, 1e-115
    0x9D9BA7832936EDC0, 0xD54B944B84AA4C0D, 0xC5029163F384A931, 0x0A9E795E65D4DF11,   // 1e-114, 1e-113
    0xF64335BCF065D37D, 0x4D4617B5FF4A16D5, 0x99EA0196163FA42E, 0x504BCED1BF8E4E45,   // 1e-112, 1e-111
    0xC06481FB9BCF8D39, 0xE45EC2862F71E1D6, 0xF07DA27A82C37088, 0x5D767327BB4E5A4C,   // 1e-110, 1e-109
    0x964E858C91BA2655, 0x3A6A07F8D510F86F, 0xBBE226EFB628AFEA, 0x890489F70A55368B,   // 1e-108, 1e-107
    0xEADAB0ABA3B2DBE5, 0x2B45AC74CCEA842E, 0x92C8AE6B464FC96F, 0x3B0B8BC90012929D,   // 1e-106, 1e-105
    0xB77ADA0617E3BBCB, 0x09CE6EBB40173744, 0xE55990879DDCAABD, 0xCC420A6A101D0515,   // 1e-104, 1e-103
    0x8F57FA54C2A9EAB6, 0x9FA946824A12232D, 0xB32DF8E9F3546564, 0x47939822DC96ABF9,   // 1e-102, 1e-101
    0xDFF9772470297EBD, 0x59787E2B93BC56F7, 0x8BFBEA76C619EF36, 0x57EB4EDB3C55B65A,   // 1e-100, 1e-99
    0xAEFAE51477A06B03, 0xEDE622920B6B23F1, 0xDAB99E59958885C4, 0xE95FAB368E45ECED,   // 1e-98, 1e-97
    0x88B402F7FD75539B, 0x11DBCB0218EBB414, 0xAAE103B5FCD2A881, 0xD652BDC29F26A119,   // 1e-96, 1e-95
    0xD59944A37C0752A2, 0x4BE76D3346F0495F, 0x857FCAE62D8493A5, 0x6F70A4400C562DDB,   // 1e-94, 1e-93
    0xA6DFBD9FB8E5B88E, 0xCB4CCD500F6BB952, 0xD097AD07A71F26B2, 0x7E2000A41346A7A7,   // 1e-92, 1e-91
    0x825ECC24C873782F, 0x8ED400668C0C28C8, 0xA2F67F2DFA90563B, 0x728900802F0F32FA,   // 1e-90, 1e-89
    0xCBB41EF979346BCA, 0x4F2B40A03AD2FFB9, 0xFEA126B7D78186BC, 0xE2F610C84987BFA8,   // 1e-88, 1e-87
    0x9F24B832E6B0F436, 0x0DD9CA7D2DF4D7C9, 0xC6EDE63FA05D3143, 0x91503D1C79720DBB,   // 1e-86, 1e-85
    0xF8A95FCF88747D94, 0x75A44C6397CE912A, 0x9B69DBE1B548CE7C, 0xC986AFBE3EE11ABA,   // 1e-84, 1e-83
    0xC24452DA229B021B, 0xFBE85BADCE996168, 0xF2D56790AB41C2A2, 0xFAE27299423FB9C3,   // 1e-82, 1e-81
    0x97C560BA6B0919A5, 0xDCCD879FC967D41A, 0xBDB6B8E905CB600F, 0x5400E987BBC1C920,   // 1e-80, 1e-79
    0xED246723473E3813, 0x290123E9AAB23B68, 0x9436C0760C86E30B, 0xF9A0B6720AAF6521,   // 1e-78, 1e-77
    0xB94470938FA89BCE, 0xF808E40E8D5B3E69, 0xE7958CB87392C2C2, 0xB60B1D1230B20E04,   // 1e-76, 1e-75
    0x90BD77F3483BB9B9, 0xB1C6F22B5E6F48C2, 0xB4ECD5F01A4AA828, 0x1E38AEB6360B1AF3,   // 1e-74, 1e-73
    0xE2280B6C20DD5232, 0x25C6DA63C38DE1B0, 0x8D590723948A535F, 0x579C487E5A38AD0E,   // 1e-72, 1e-71
    0xB0AF48EC79ACE837, 0x2D835A9DF0C6D851, 0xDCDB1B2798182244, 0xF8E431456CF88E65,   // 1e-70, 1e-69
    0x8A08F0F8BF0F156B, 0x1B8E9ECB641B58FF, 0xAC8B2D36EED2DAC5, 0xE272467E3D222F3F,   // 1e-68, 1e-67
    0xD7ADF884AA879177, 0x5B0ED81DCC6ABB0F, 0x86CCBB52EA94BAEA, 0x98E947129FC2B4E9,   // 1e-66, 1e-65
    0xA87FEA27A539E9A5, 0x3F2398D747B36224, 0xD29FE4B18E88640E, 0x8EEC7F0D19A03AAD,   // 1e-64, 1e-63
    0x83A3EEEEF9153E89, 0x1953CF68300424AC, 0xA48CEAAAB75A8E2B, 0x5FA8C3423C052DD7,   // 1e-62, 1e-61
    0xCDB02555653131B6, 0x3792F412CB06794D, 0x808E17555F3EBF11, 0xE2BBD88BBEE40BD0,   // 1e-60, 1e-59
    0xA0B19D2AB70E6ED6, 0x5B6ACEAEAE9D0EC4, 0xC8DE047564D20A8B, 0xF245825A5A445275,   // 1e-58, 1e-57
    0xFB158592BE068D2E, 0xEED6E2F0F0D56712, 0x9CED737BB6C4183D, 0x55464DD69685606B,   // 1e-56, 1e-55
    0xC428D05AA4751E4C, 0xAA97E14C3C26B886, 0xF53304714D9265DF, 0xD53DD99F4B3066A8,   // 1e-54, 1e-53
    0x993FE2C6D07B7FAB, 0xE546A8038EFE4029, 0xBF8FDB78849A5F96, 0xDE98520472BDD033,   // 1e-52, 1e-51
    0xEF73D256A5C0F77C, 0x963E66858F6D4440, 0x95A8637627989AAD, 0xDDE7001379A44AA8,   // 1e-50, 1e-49
    0xBB127C53B17EC159, 0x5560C018580D5D52, 0xE9D71B689DDE71AF, 0xAAB8F01E6E10B4A6,   // 1e-48, 1e-47
    0x9226712162AB070D, 0xCAB3961304CA70E8, 0xB6B00D69BB55C8D1, 0x3D607B97C5FD0D22,   // 1e-46, 1e-45
    0xE45C10C42A2B3B05, 0x8CB89A7DB77C506A, 0x8EB98A7A9A5B04E3, 0x77F3608E92ADB242,   // 1e-44, 1e-43
    0xB267ED1940F1C61C, 0x55F038B237591ED3, 0xDF01E85F912E37A3, 0x6B6C46DEC52F6688,   // 1e-42, 1e-41
    0x8B61313BBABCE2C6, 0x2323AC4B3B3DA015, 0xAE397D8AA96C1B77, 0xABEC975E0A0D081A,   // 1e-40, 1e-39
    0xD9C7DCED53C72255, 0x96E7BD358C904A21, 0x881CEA14545C7575, 0x7E50D64177DA2E54,   // 1e-38, 1e-37
    0xAA242499697392D2, 0xDDE50BD1D5D0B9E9, 0xD4AD2DBFC3D07787, 0x955E4EC64B44E864,   // 1e-36, 1e-35
    0x84EC3C97DA624AB4, 0xBD5AF13BEF0B113E, 0xA6274BBDD0FADD61, 0xECB1AD8AEACDD58E,   // 1e-34, 1e-33
    0xCFB11EAD453994BA, 0x67DE18EDA5814AF2, 0x81CEB32C4B43FCF4, 0x80EACF948770CED7,   // 1e-32, 1e-31
    0xA2425FF75E14FC31, 0xA1258379A94D028D, 0xCAD2F7F5359A3B3E, 0x096EE45813A04330,   // 1e-30, 1e-29
    0xFD87B5F28300CA0D, 0x8BCA9D6E188853FC, 0x9E74D1B791E07E48, 0x775EA264CF55347D,   // 1e-28, 1e-27
    0xC612062576589DDA, 0x95364AFE032A819D, 0xF79687AED3EEC551, 0x3A83DDBD83F52204,   // 1e-26, 1e-25
    0x9ABE14CD44753B52, 0xC4926A9672793542, 0xC16D9A0095928A27, 0x75B7053C0F178293,   // 1e-24, 1e-23
    0xF1C90080BAF72CB1, 0x5324C68B12DD6338, 0x971DA05074DA7BEE, 0xD3F6FC16EBCA5E03,   // 1e-22, 1e-21
    0xBCE5086492111AEA, 0x88F4BB1CA6BCF584, 0xEC1E4A7DB69561A5, 0x2B31E9E3D06C32E5,   // 1e-20, 1e-19
    0x9392EE8E921D5D07, 0x3AFF322E62439FCF, 0xB877AA3236A4B449, 0x09BEFEB9FAD487C2,   // 1e-18, 1e-17
    0xE69594BEC44DE15B, 0x4C2EBE687989A9B3, 0x901D7CF73AB0ACD9, 0x0F9D37014BF60A10,   // 1e-16, 1e-15
    0xB424DC35095CD80F, 0x538484C19EF38C94, 0xE12E13424BB40E13, 0x2865A5F206B06FB9,   // 1e-14, 1e-13
    0x8CBCCC096F5088CB, 0xF93F87B7442E45D3, 0xAFEBFF0BCB24AAFE, 0xF78F69A51539D748,   // 1e-12, 1e-11
    0xDBE6FECEBDEDD5BE, 0xB573440E5A884D1B, 0x89705F4136B4A597, 0x31680A88F8953030,   // 1e-10, 1e-9
    0xABCC77118461CEFC, 0xFDC20D2B36BA7C3D, 0xD6BF94D5E57A42BC, 0x3D32907604691B4C,   // 1e-8, 1e-7
    0x8637BD05AF6C69B5, 0xA63F9A49C2C1B10F, 0xA7C5AC471B478423, 0x0FCF80DC33721D53,   // 1e-6, 1e-5
    0xD1B71758E219652B, 0xD3C36113404EA4A8, 0x83126E978D4FDF3B, 0x645A1CAC083126E9,   // 1e-4, 1e-3
    0xA3D70A3D70A3D70A, 0x3D70A3D70A3D70A3, 0xCCCCCCCCCCCCCCCC, 0xCCCCCCCCCCCCCCCC,   // 1e-2, 1e-1
    0x8000000000000000, 0x0000000000000000, 0xA000000000000000, 0x0000000000000000,   // 1e0, 1e1
    0xC800000000000000, 0x0000000000000000, 0xFA00000000000000, 0x0000000000000000,   // 1e2, 1e3
    0x9C40000000000000, 0x0000000000000000, 0xC350000000000000, 0x0000000000000000,   // 1e4, 1e5
    0xF424000000000000, 0x0000000000000000, 0x9896800000000000, 0x0000000000000000,   // 1e6, 1e7
    0xBEBC200000000000, 0x0000000000000000, 0xEE6B280000000000, 0x0000000000000000,   // 1e8, 1e9
    0x9502F90000000000, 0x0000000000000000, 0xBA43B74000000000, 0x0000000000000000,   // 1e10, 1e11
    0xE8D4A51000000000, 0x0000000000000000, 0x9184E72A00000000, 0x0000000000000000,   // 1e12, 1e13
    0xB5E620F480000000, 0x0000000000000000, 0xE35FA931A0000000, 0x0000000000000000,   // 1e14, 1e15
    0x8E1BC9BF04000000, 0x0000000000000000, 0xB1A2BC2EC5000000, 0x0000000000000000,   // 1e16, 1e17
    0xDE0B6B3A76400000, 0x0000000000000000, 0x8AC7230489E80000, 0x0000000000000000,   // 1e18, 1e19
    0xAD78EBC5AC620000, 0x0000000000000000, 0xD8D726B7177A8000, 0x0000000000000000,   // 1e20, 1e21
    0x878678326EAC9000, 0x0000000000000000, 0xA968163F0A57B400, 0x0000000000000000,   // 1e22, 1e23
    0xD3C21BCECCEDA100, 0x0000000000000000, 0x84595161401484A0, 0x0000000000000000,   // 1e24, 1e25
    0xA56FA5B99019A5C8, 0x0000000000000000, 0xCECB8F27F4200F3A, 0x0000000000000000,   // 1e26, 1e27
    0x813F3978F8940984, 0x4000000000000000, 0xA18F07D736B90BE5, 0x5000000000000000,   // 1e28, 1e29
    0xC9F2C9CD04674EDE, 0xA400000000000000, 0xFC6F7C4045812296, 0x4D00000000000000,   // 1e30, 1e31
    0x9DC5ADA82B70B59D, 0xF020000000000000, 0xC5371912364CE305, 0x6C28000000000000,   // 1e32, 1e33
    0xF684DF56C3E01BC6, 0xC732000000000000, 0x9A130B963A6C115C, 0x3C7F400000000000,   // 1e34, 1e35
    0xC097CE7BC90715B3, 0x4B9F100000000000, 0xF0BDC21ABB48DB20, 0x1E86D40000000000,   // 1e36, 1e37
    0x96769950B50D88F4, 0x1314448000000000, 0xBC143FA4E250EB31, 0x17D955A000000000,   // 1e38, 1e39
    0xEB194F8E1AE525FD, 0x5DCFAB0800000000, 0x92EFD1B8D0CF37BE, 0x5AA1CAE500000000,   // 1e40, 1e41
    0xB7ABC627050305AD, 0xF14A3D9E40000000, 0xE596B7B0C643C719, 0x6D9CCD05D0000000,   // 1e42, 1e43
    0x8F7E32CE7BEA5C6F, 0xE4820023A2000000, 0xB35DBF821AE4F38B, 0xDDA2802C8A800000,   // 1e44, 1e45
    0xE0352F62A19E306E, 0xD50B2037AD200000, 0x8C213D9DA502DE45, 0x4526F422CC340000,   // 1e46, 1e47
    0xAF298D050E4395D6, 0x9670B12B7F410000, 0xDAF3F04651D47B4C, 0x3C0CDD765F114000,   // 1e48, 1e49
    0x88D8762BF324CD0F, 0xA5880A69FB6AC800, 0xAB0E93B6EFEE0053, 0x8EEA0D047A457A00,   // 1e50, 1e51
    0xD5D238A4ABE98068, 0x72A4904598D6D880, 0x85A36366EB71F041, 0x47A6DA2B7F864750,   // 1e52, 1e53
    0xA70C3C40A64E6C51, 0x999090B65F67D924, 0xD0CF4B50CFE20765, 0xFFF4B4E3F741CF6D,   // 1e54, 1e55
    0x82818F1281ED449F, 0xBFF8F10E7A8921A4, 0xA321F2D7226895C7, 0xAFF72D52192B6A0D,   // 1e56, 1e57
    0xCBEA6F8CEB02BB39, 0x9BF4F8A69F764490, 0xFEE50B7025C36A08, 0x02F236D04753D5B4,   // 1e58, 1e59
    0x9F4F2726179A2245, 0x01D762422C946590, 0xC722F0EF9D80AAD6, 0x424D3AD2B7B97EF5,   // 1e60, 1e61
    0xF8EBAD2B84E0D58B, 0xD2E0898765A7DEB2, 0x9B934C3B330C8577, 0x63CC55F49F88EB2F,   // 1e62, 1e63
    0xC2781F49FFCFA6D5, 0x3CBF6B71C76B25FB, 0xF316271C7FC3908A, 0x8BEF464E3945EF7A,   // 1e64, 1e65
    0x97EDD871CFDA3A56, 0x97758BF0E3CBB5AC, 0xBDE94E8E43D0C8EC, 0x3D52EEED1CBEA317,   // 1e66, 1e67
    0xED63A231D4C4FB27, 0x4CA7AAA863EE4BDD, 0x945E455F24FB1CF8, 0x8FE8CAA93E74EF6A,   // 1e68, 1e69
    0xB975D6B6EE39E436, 0xB3E2FD538E122B44, 0xE7D34C64A9C85D44, 0x60DBBCA87196B616,   // 1e70, 1e71
    0x90E40FBEEA1D3A4A, 0xBC8955E946FE31CD, 0xB51D13AEA4A488DD, 0x6BABAB6398BDBE41,   // 1e72, 1e73
    0xE264589A4DCDAB14, 0xC696963C7EED2DD1, 0x8D7EB76070A08AEC, 0xFC1E1DE5CF543CA2,   // 1e74, 1e75
    0xB0DE65388CC8ADA8, 0x3B25A55F43294BCB, 0xDD15FE86AFFAD912, 0x49EF0EB713F39EBE,   // 1e76, 1e77
    0x8A2DBF142DFCC7AB, 0x6E3569326C784337, 0xACB92ED9397BF996, 0x49C2C37F07965404,   // 1e78, 1e79
    0xD7E77A8F87DAF7FB, 0xDC33745EC97BE906, 0x86F0AC99B4E8DAFD, 0x69A028BB3DED71A3,   // 1e80, 1e81
    0xA8ACD7C0222311BC, 0xC40832EA0D68CE0C, 0xD2D80DB02AABD62B, 0xF50A3FA490C30190,   // 1e82, 1e83
    0x83C7088E1AAB65DB, 0x792667C6DA79E0FA, 0xA4B8CAB1A1563F52, 0x577001B891185938,   // 1e84, 1e85
    0xCDE6FD5E09ABCF26, 0xED4C0226B55E6F86, 0x80B05E5AC60B6178, 0x544F8158315B05B4,   // 1e86, 1e87
    0xA0DC75F1778E39D6, 0x696361AE3DB1C721, 0xC913936DD571C84C, 0x03BC3A19CD1E38E9,   // 1e88, 1e89
    0xFB5878494ACE3A5F, 0x04AB48A04065C723, 0x9D174B2DCEC0E47B, 0x62EB0D64283F9C76,   // 1e90, 1e91
    0xC45D1DF942711D9A, 0x3BA5D0BD324F8394, 0xF5746577930D6500, 0xCA8F44EC7EE36479,   // 1e92, 1e93
    0x9968BF6ABBE85F20, 0x7E998B13CF4E1ECB, 0xBFC2EF456AE276E8, 0x9E3FEDD8C321A67E,   // 1e94, 1e95
    0xEFB3AB16C59B14A2, 0xC5CFE94EF3EA101E, 0x95D04AEE3B80ECE5, 0xBBA1F1D158724A12,   // 1e96, 1e97
    0xBB445DA9CA61281F, 0x2A8A6E45AE8EDC97, 0xEA1575143CF97226, 0xF52D09D71A3293BD,   // 1e98, 1e99
    0x924D692CA61BE758, 0x593C2626705F9C56, 0xB6E0C377CFA2E12E, 0x6F8B2FB00C77836C,   // 1e100, 1e101
    0xE498F455C38B997A, 0x0B6DFB9C0F956447, 0x8EDF98B59A373FEC, 0x4724BD4189BD5EAC,   // 1e102, 1e103
    0xB2977EE300C50FE7, 0x58EDEC91EC2CB657, 0xDF3D5E9BC0F653E1, 0x2F2967B66737E3ED,   // 1e104, 1e105
    0x8B865B215899F46C, 0xBD79E0D20082EE74, 0xAE67F1E9AEC07187, 0xECD8590680A3AA11,   // 1e106, 1e107
    0xDA01EE641A708DE9, 0xE80E6F4820CC9495, 0x884134FE908658B2, 0x3109058D147FDCDD,   // 1e108, 1e109
    0xAA51823E34A7EEDE, 0xBD4B46F0599FD415, 0xD4E5E2CDC1D1EA96, 0x6C9E18AC7007C91A,   // 1e110, 1e111
    0x850FADC09923329E, 0x03E2CF6BC604DDB0, 0xA6539930BF6BFF45, 0x84DB8346B786151C,   // 1e112, 1e113
    0xCFE87F7CEF46FF16, 0xE612641865679A63, 0x81F14FAE158C5F6E, 0x4FCB7E8F3F60C07E,   // 1e114, 1e115
    0xA26DA3999AEF7749, 0xE3BE5E330F38F09D, 0xCB090C8001AB551C, 0x5CADF5BFD3072CC5,   // 1e116, 1e117
    0xFDCB4FA002162A63, 0x73D9732FC7C8F7F6, 0x9E9F11C4014DDA7E, 0x2867E7FDDCDD9AFA,   // 1e118, 1e119
    0xC646D63501A1511D, 0xB281E1FD541501B8, 0xF7D88BC24209A565, 0x1F225A7CA91A4226,   // 1e120, 1e121
    0x9AE757596946075F, 0x3375788DE9B06958, 0xC1A12D2FC3978937, 0x0052D6B1641C83AE,   // 1e122, 1e123
    0xF209787BB47D6B84, 0xC0678C5DBD23A49A, 0x9745EB4D50CE6332, 0xF840B7BA963646E0,   // 1e124, 1e125
    0xBD176620A501FBFF, 0xB650E5A93BC3D898, 0xEC5D3FA8CE427AFF, 0xA3E51F138AB4CEBE,   // 1e126, 1e127
    0x93BA47C980E98CDF, 0xC66F336C36B10137, 0xB8A8D9BBE123F017, 0xB80B0047445D4184,   // 1e128, 1e129
    0xE6D3102AD96CEC1D, 0xA60DC059157491E5, 0x9043EA1AC7E41392, 0x87C89837AD68DB2F,   // 1e130, 1e131
    0xB454E4A179DD1877, 0x29BABE4598C311FB, 0xE16A1DC9D8545E94, 0xF4296DD6FEF3D67A,   // 1e132, 1e133
    0x8CE2529E2734BB1D, 0x1899E4A65F58660C, 0xB01AE745B101E9E4, 0x5EC05DCFF72E7F8F,   // 1e134, 1e135
    0xDC21A1171D42645D, 0x76707543F4FA1F73, 0x899504AE72497EBA, 0x6A06494A791C53A8,   // 1e136, 1e137
    0xABFA45DA0EDBDE69, 0x0487DB9D17636892, 0xD6F8D7509292D603, 0x45A9D2845D3C42B6,   // 1e138, 1e139
    0x865B86925B9BC5C2, 0x0B8A2392BA45A9B2, 0xA7F26836F282B732, 0x8E6CAC7768D7141E,   // 1e140, 1e141
    0xD1EF0244AF2364FF, 0x3207D795430CD926, 0x8335616AED761F1F, 0x7F44E6BD49E807B8,   // 1e142, 1e143
    0xA402B9C5A8D3A6E7, 0x5F16206C9C6209A6, 0xCD036837130890A1, 0x36DBA887C37A8C0F,   // 1e144, 1e145
    0x802221226BE55A64, 0xC2494954DA2C9789, 0xA02AA96B06DEB0FD, 0xF2DB9BAA10B7BD6C,   // 1e146, 1e147
    0xC83553C5C8965D3D, 0x6F92829494E5ACC7, 0xFA42A8B73ABBF48C, 0xCB772339BA1F17F9,   // 1e148, 1e149
    0x9C69A97284B578D7, 0xFF2A760414536EFB, 0xC38413CF25E2D70D, 0xFEF5138519684ABA,   // 1e150, 1e151
    0xF46518C2EF5B8CD1, 0x7EB258665FC25D69, 0x98BF2F79D5993802, 0xEF2F773FFBD97A61,   // 1e152, 1e153
    0xBEEEFB584AFF8603, 0xAAFB550FFACFD8FA, 0xEEAABA2E5DBF6784, 0x95BA2A53F983CF38,   // 1e154, 1e155
    0x952AB45CFA97A0B2, 0xDD945A747BF26183, 0xBA756174393D88DF, 0x94F971119AEEF9E4,   // 1e156, 1e157
    0xE912B9D1478CEB17, 0x7A37CD5601AAB85D, 0x91ABB422CCB812EE, 0xAC62E055C10AB33A,   // 1e158, 1e159
    0xB616A12B7FE617AA, 0x577B986B314D6009, 0xE39C49765FDF9D94, 0xED5A7E85FDA0B80B,   // 1e160, 1e161
    0x8E41ADE9FBEBC27D, 0x14588F13BE847307, 0xB1D219647AE6B31C, 0x596EB2D8AE258FC8,   // 1e162, 1e163
    0xDE469FBD99A05FE3, 0x6FCA5F8ED9AEF3BB, 0x8AEC23D680043BEE, 0x25DE7BB9480D5854,   // 1e164, 1e165
    0xADA72CCC20054AE9, 0xAF561AA79A10AE6A, 0xD910F7FF28069DA4, 0x1B2BA1518094DA04,   // 1e166, 1e167
    0x87AA9AFF79042286, 0x90FB44D2F05D0842, 0xA99541BF57452B28, 0x353A1607AC744A53,   // 1e168, 1e169
    0xD3FA922F2D1675F2, 0x42889B8997915CE8, 0x847C9B5D7C2E09B7, 0x69956135FEBADA11,   // 1e170, 1e171
    0xA59BC234DB398C25, 0x43FAB9837E699095, 0xCF02B2C21207EF2E, 0x94F967E45E03F4BB,   // 1e172, 1e173
    0x8161AFB94B44F57D, 0x1D1BE0EEBAC278F5, 0xA1BA1BA79E1632DC, 0x6462D92A69731732,   // 1e174, 1e175
    0xCA28A291859BBF93, 0x7D7B8F7503CFDCFE, 0xFCB2CB35E702AF78, 0x5CDA735244C3D43E,   // 1e176, 1e177
    0x9DEFBF01B061ADAB, 0x3A0888136AFA64A7, 0xC56BAEC21C7A1916, 0x088AAA1845B8FDD0,   // 1e178, 1e179
    0xF6C69A72A3989F5B, 0x8AAD549E57273D45, 0x9A3C2087A63F6399, 0x36AC54E2F678864B,   // 1e180, 1e181
    0xC0CB28A98FCF3C7F, 0x84576A1BB416A7DD, 0xF0FDF2D3F3C30B9F, 0x656D44A2A11C51D5,   // 1e182, 1e183
    0x969EB7C47859E743, 0x9F644AE5A4B1B325, 0xBC4665B596706114, 0x873D5D9F0DDE1FEE,   // 1e184, 1e185
    0xEB57FF22FC0C7959, 0xA90CB506D155A7EA, 0x9316FF75DD87CBD8, 0x09A7F12442D588F2,   // 1e186, 1e187
    0xB7DCBF5354E9BECE, 0x0C11ED6D538AEB2F, 0xE5D3EF282A242E81, 0x8F1668C8A86DA5FA,   // 1e188, 1e189
    0x8FA475791A569D10, 0xF96E017D694487BC, 0xB38D92D760EC4455, 0x37C981DCC395A9AC,   // 1e190, 1e191
    0xE070F78D3927556A, 0x85BBE253F47B1417, 0x8C469AB843B89562, 0x93956D7478CCEC8E,   // 1e192, 1e193
    0xAF58416654A6BABB, 0x387AC8D1970027B2, 0xDB2E51BFE9D0696A, 0x06997B05FCC0319E,   // 1e194, 1e195
    0x88FCF317F22241E2, 0x441FECE3BDF81F03, 0xAB3C2FDDEEAAD25A, 0xD527E81CAD7626C3,   // 1e196, 1e197
    0xD60B3BD56A5586F1, 0x8A71E223D8D3B074, 0x85C7056562757456, 0xF6872D5667844E49,   // 1e198, 1e199
    0xA738C6BEBB12D16C, 0xB428F8AC016561DB, 0xD106F86E69D785C7, 0xE13336D701BEBA52,   // 1e200, 1e201
    0x82A45B450226B39C, 0xECC0024661173473, 0xA34D721642B06084, 0x27F002D7F95D0190,   // 1e202, 1e203
    0xCC20CE9BD35C78A5, 0x31EC038DF7B441F4, 0xFF290242C83396CE, 0x7E67047175A15271,   // 1e204, 1e205
    0x9F79A169BD203E41, 0x0F0062C6E984D386, 0xC75809C42C684DD1, 0x52C07B78A3E60868,   // 1e206, 1e207
    0xF92E0C3537826145, 0xA7709A56CCDF8A82, 0x9BBCC7A142B17CCB, 0x88A66076400BB691,   // 1e208, 1e209
    0xC2ABF989935DDBFE, 0x6ACFF893D00EA435, 0xF356F7EBF83552FE, 0x0583F6B8C4124D43,   // 1e210, 1e211
    0x98165AF37B2153DE, 0xC3727A337A8B704A, 0xBE1BF1B059E9A8D6, 0x744F18C0592E4C5C,   // 1e212, 1e213
    0xEDA2EE1C7064130C, 0x1162DEF06F79DF73, 0x9485D4D1C63E8BE7, 0x8ADDCB5645AC2BA8,   // 1e214, 1e215
    0xB9A74A0637CE2EE1, 0x6D953E2BD7173692, 0xE8111C87C5C1BA99, 0xC8FA8DB6CCDD0437,   // 1e216, 1e217
    0x910AB1D4DB9914A0, 0x1D9C9892400A22A2, 0xB54D5E4A127F59C8, 0x2503BEB6D00CAB4B,   // 1e218, 1e219
    0xE2A0B5DC971F303A, 0x2E44AE64840FD61D, 0x8DA471A9DE737E24, 0x5CEAECFED289E5D2,   // 1e220, 1e221
    0xB10D8E1456105DAD, 0x7425A83E872C5F47, 0xDD50F1996B947518, 0xD12F124E28F77719,   // 1e222, 1e223
    0x8A5296FFE33CC92F, 0x82BD6B70D99AAA6F, 0xACE73CBFDC0BFB7B, 0x636CC64D1001550B,   // 1e224, 1e225
    0xD8210BEFD30EFA5A, 0x3C47F7E05401AA4E, 0x8714A775E3E95C78, 0x65ACFAEC34810A71,   // 1e226, 1e227
    0xA8D9D1535CE3B396, 0x7F1839A741A14D0D, 0xD31045A8341CA07C, 0x1EDE48111209A050,   // 1e228, 1e229
    0x83EA2B892091E44D, 0x934AED0AAB460432, 0xA4E4B66B68B65D60, 0xF81DA84D5617853F,   // 1e230, 1e231
    0xCE1DE40642E3F4B9, 0x36251260AB9D668E, 0x80D2AE83E9CE78F3, 0xC1D72B7C6B426019,   // 1e232, 1e233
    0xA1075A24E4421730, 0xB24CF65B8612F81F, 0xC94930AE1D529CFC, 0xDEE033F26797B627,   // 1e234, 1e235
    0xFB9B7CD9A4A7443C, 0x169840EF017DA3B1, 0x9D412E0806E88AA5, 0x8E1F289560EE864E,   // 1e236, 1e237
    0xC491798A08A2AD4E, 0xF1A6F2BAB92A27E2, 0xF5B5D7EC8ACB58A2, 0xAE10AF696774B1DB,   // 1e238, 1e239
    0x9991A6F3D6BF1765, 0xACCA6DA1E0A8EF29, 0xBFF610B0CC6EDD3F, 0x17FD090A58D32AF3,   // 1e240, 1e241
    0xEFF394DCFF8A948E, 0xDDFC4B4CEF07F5B0, 0x95F83D0A1FB69CD9, 0x4ABDAF101564F98E,   // 1e242, 1e243
    0xBB764C4CA7A4440F, 0x9D6D1AD41ABE37F1, 0xEA53DF5FD18D5513, 0x84C86189216DC5ED,   // 1e244, 1e245
    0x92746B9BE2F8552C, 0x32FD3CF5B4E49BB4, 0xB7118682DBB66A77, 0x3FBC8C33221DC2A1,   // 1e246, 1e247
    0xE4D5E82392A40515, 0x0FABAF3FEAA5334A, 0x8F05B1163BA6832D, 0x29CB4D87F2A7400E,   // 1e248, 1e249
    0xB2C71D5BCA9023F8, 0x743E20E9EF511012, 0xDF78E4B2BD342CF6, 0x914DA9246B255416,   // 1e250, 1e251
    0x8BAB8EEFB6409C1A, 0x1AD089B6C2F7548E, 0xAE9672ABA3D0C320, 0xA184AC2473B529B1,   // 1e252, 1e253
    0xDA3C0F568CC4F3E8, 0xC9E5D72D90A2741E, 0x8865899617FB1871, 0x7E2FA67C7A658892,   // 1e254, 1e255
    0xAA7EEBFB9DF9DE8D, 0xDDBB901B98FEEAB7, 0xD51EA6FA85785631, 0x552A74227F3EA565,   // 1e256, 1e257
    0x8533285C936B35DE, 0xD53A88958F87275F, 0xA67FF273B8460356, 0x8A892ABAF368F137,   // 1e258, 1e259
    0xD01FEF10A657842C, 0x2D2B7569B0432D85, 0x8213F56A67F6B29B, 0x9C3B29620E29FC73,   // 1e260, 1e261
    0xA298F2C501F45F42, 0x8349F3BA91B47B8F, 0xCB3F2F7642717713, 0x241C70A936219A73,   // 1e262, 1e263
    0xFE0EFB53D30DD4D7, 0xED238CD383AA0110, 0x9EC95D1463E8A506, 0xF4363804324A40AA,   // 1e264, 1e265
    0xC67BB4597CE2CE48, 0xB143C6053EDCD0D5, 0xF81AA16FDC1B81DA, 0xDD94B7868E94050A,   // 1e266, 1e267
    0x9B10A4E5E9913128, 0xCA7CF2B4191C8326, 0xC1D4CE1F63F57D72, 0xFD1C2F611F63A3F0,   // 1e268, 1e269
    0xF24A01A73CF2DCCF, 0xBC633B39673C8CEC, 0x976E41088617CA01, 0xD5BE0503E085D813,   // 1e270, 1e271
    0xBD49D14AA79DBC82, 0x4B2D8644D8A74E18, 0xEC9C459D51852BA2, 0xDDF8E7D60ED1219E,   // 1e272, 1e273
    0x93E1AB8252F33B45, 0xCABB90E5C942B503, 0xB8DA1662E7B00A17, 0x3D6A751F3B936243,   // 1e274, 1e275
    0xE7109BFBA19C0C9D, 0x0CC512670A783AD4, 0x906A617D450187E2, 0x27FB2B80668B24C5,   // 1e276, 1e277
    0xB484F9DC9641E9DA, 0xB1F9F660802DEDF6, 0xE1A63853BBD26451, 0x5E7873F8A0396973,   // 1e278, 1e279
    0x8D07E33455637EB2, 0xDB0B487B6423E1E8, 0xB049DC016ABC5E5F, 0x91CE1A9A3D2CDA62,   // 1e280, 1e281
    0xDC5C5301C56B75F7, 0x7641A140CC7810FB, 0x89B9B3E11B6329BA, 0xA9E904C87FCB0A9D,   // 1e282, 1e283
    0xAC2820D9623BF429, 0x546345FA9FBDCD44, 0xD732290FBACAF133, 0xA97C177947AD4095,   // 1e284, 1e285
    0x867F59A9D4BED6C0, 0x49ED8EABCCCC485D, 0xA81F301449EE8C70, 0x5C68F256BFFF5A74,   // 1e286, 1e287
    0xD226FC195C6A2F8C, 0x73832EEC6FFF3111, 0x83585D8FD9C25DB7, 0xC831FD53C5FF7EAB,   // 1e288, 1e289
    0xA42E74F3D032F525, 0xBA3E7CA8B77F5E55, 0xCD3A1230C43FB26F, 0x28CE1BD2E55F35EB,   // 1e290, 1e291
    0x80444B5E7AA7CF85, 0x7980D163CF5B81B3, 0xA0555E361951C366, 0xD7E105BCC332621F,   // 1e292, 1e293
    0xC86AB5C39FA63440, 0x8DD9472BF3FEFAA7, 0xFA856334878FC150, 0xB14F98F6F0FEB951,   // 1e294, 1e295
    0x9C935E00D4B9D8D2, 0x6ED1BF9A569F33D3, 0xC3B8358109E84F07, 0x0A862F80EC4700C8,   // 1e296, 1e297
    0xF4A642E14C6262C8, 0xCD27BB612758C0FA, 0x98E7E9CCCFBD7DBD, 0x8038D51CB897789C,   // 1e298, 1e299
    0xBF21E44003ACDD2C, 0xE0470A63E6BD56C3, 0xEEEA5D5004981478, 0x1858CCFCE06CAC74,   // 1e300, 1e301
    0x95527A5202DF0CCB, 0x0F37801E0C43EBC8, 0xBAA718E68396CFFD, 0xD30560258F54E6BA,   // 1e302, 1e303
    0xE950DF20247C83FD, 0x47C6B82EF32A2069, 0x91D28B7416CDD27E, 0x4CDC331D57FA5441,   // 1e304, 1e305
    0xB6472E511C81471D, 0xE0133FE4ADF8E952, 0xE3D8F9E563A198E5, 0x58180FDDD97723A6,   // 1e306, 1e307
    0x8E679C2F5E44FF8F, 0x570F09EAA7EA7648, 0xB201833B35D63F73, 0x2CD2CC6551E513DA,   // 1e308, 1e309
    0xDE81E40A034BCF4F, 0xF8077F7EA65E58D1, 0x8B112E86420F6191, 0xFB04AFAF27FAF782,   // 1e310, 1e311
    0xADD57A27D29339F6, 0x79C5DB9AF1F9B563, 0xD94AD8B1C7380874, 0x18375281AE7822BC,   // 1e312, 1e313
    0x87CEC76F1C830548, 0x8F2293910D0B15B5, 0xA9C2794AE3A3C69A, 0xB2EB3875504DDB22,   // 1e314, 1e315
    0xD433179D9C8CB841, 0x5FA60692A46151EB, 0x849FEEC281D7F328, 0xDBC7C41BA6BCD333,   // 1e316, 1e317
    0xA5C7EA73224DEFF3, 0x12B9B522906C0800, 0xCF39E50FEAE16BEF, 0xD768226B34870A00,   // 1e318, 1e319
    0x81842F29F2CCE375, 0xE6A1158300D46640, 0xA1E53AF46F801C53, 0x60495AE3C1097FD0,   // 1e320, 1e321
    0xCA5E89B18B602368, 0x385BB19CB14BDFC4, 0xFCF62C1DEE382C42, 0x46729E03DD9ED7B5,   // 1e322, 1e323
    0x9E19DB92B4E31BA9, 0x6C07A2C26A8346D1, 0xC5A05277621BE293, 0xC7098B7305241885,   // 1e324, 1e325
    0xF70867153AA2DB38, 0xB8CBEE4FC66D1EA7, 0x9A65406D44A5C903, 0x737F74F1DC043328,   // 1e326, 1e327
    0xC0FE908895CF3B44, 0x505F522E53053FF2, 0xF13E34AABB430A15, 0x647726B9E7C68FEF,   // 1e328, 1e329
    0x96C6E0EAB509E64D, 0x5ECA783430DC19F5, 0xBC789925624C5FE0, 0xB67D16413D132072,   // 1e330, 1e331
    0xEB96BF6EBADF77D8, 0xE41C5BD18C57E88F, 0x933E37A534CBAAE7, 0x8E91B962F7B6F159,   // 1e332, 1e333
    0xB80DC58E81FE95A1, 0x723627BBB5A4ADB0, 0xE61136F2227E3B09, 0xCEC3B1AAA30DD91C,   // 1e334, 1e335
    0x8FCAC257558EE4E6, 0x213A4F0AA5E8A7B1, 0xB3BD72ED2AF29E1F, 0xA988E2CD4F62D19D,   // 1e336, 1e337
    0xE0ACCFA875AF45A7, 0x93EB1B80A33B8605, 0x8C6C01C9498D8B88, 0xBC72F130660533C3,   // 1e338, 1e339
    0xAF87023B9BF0EE6A, 0xEB8FAD7C7F8680B4, 0xDB68C2CA82ED2A05, 0xA67398DB9F6820E1,   // 1e340, 1e341
    0x892179BE91D43A43, 0x88083F8943A1148C, 0xAB69D82E364948D4, 0x6A0A4F6B948959B0,   // 1e342, 1e343
    0xD6444E39C3DB9B09, 0x848CE34679ABB01C, 0x85EAB0E41A6940E5, 0xF2D80E0C0C0B4E11,   // 1e344, 1e345
    0xA7655D1D2103911F, 0x6F8E118F0F0E2195, 0xD13EB46469447567, 0x4B7195F2D2D1A9FB,   // 1e346, 1e347
];
