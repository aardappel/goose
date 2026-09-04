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

fn swap<T>(a: T&, b: T&) {
    let t = a;
    a = b;
    b = t;
}

// ---- hashing -------------------------------------------------------------
// One overload per scalar type: a u8 widens equally well into i64 and u64,
// so a single generic would be ambiguous. Integers get a multiply-xorshift
// mix (the low bits are what `dictionary` masks), bytes FNV-1a.

fn hash_mix(x: u64) -> u64 {
    var h = x * 11400714819323198485;
    h = h ^ (h >> 32);
    return h;
}

fn hash(x: i8) -> u64 { hash_mix(x as u64) }
fn hash(x: i16) -> u64 { hash_mix(x as u64) }
fn hash(x: i32) -> u64 { hash_mix(x as u64) }
fn hash(x: i64) -> u64 { hash_mix(x as u64) }
fn hash(x: u8) -> u64 { hash_mix(x as u64) }
fn hash(x: u16) -> u64 { hash_mix(x as u64) }
fn hash(x: u32) -> u64 { hash_mix(x as u64) }
fn hash(x: u64) -> u64 { hash_mix(x) }
fn hash(x: bool) -> u64 { if x { return hash_mix(1); } return hash_mix(2); }
fn hash(x: f32) -> u64 { hash_mix((x * 65536.0) as i64 as u64) }
fn hash(x: f64) -> u64 { hash_mix((x * 65536.0) as i64 as u64) }

fn hash(s: u8[:]) -> u64 {
    var h: u64 = 14695981039346656037;
    for b in s { h = (h ^ (b as u64)) * 1099511628211; }
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
fn rand_flt(r: rng&) -> f64 { ((rand_u64(r) >> 11) as f64) * (1.0 / 9007199254740992.0) }

// Fisher-Yates.
fn shuffle<T>(xs: T[:], r: rng&) {
    var i = xs.len - 1;
    while i > 0 {
        let j = rand_int(r, i + 1);
        let t = xs[i];
        xs[i] = xs[j];
        xs[j] = t;
        i -= 1;
    }
}

// ---- arrays: reading and searching -------------------------------------------

fn swap_at<T>(xs: T[:], i: i64, j: i64) {
    let t = xs[i];
    xs[i] = xs[j];
    xs[j] = t;
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

fn fill<T>(xs: T[:], v: T) { for &x in xs { x = v; } }

// Element by element, first to last; the lengths must agree.
fn copy_into<T>(dst: T[:], src: T[:]) {
    assert(dst.len == src.len);
    for i in dst.len { dst[i] = src[i]; }
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

// Quicksort: median-of-three pivots, insertion sort below 16 elements, an
// explicit range stack that always defers the larger partition (so it
// never recurses and the stack stays logarithmic). Unstable, in place.
fn sort<T>(xs: T[:]) { sort(xs) { a, b => a < b } }
fn sort<T, F>(xs: T[:]) {
    var st: i64[..128] = [];
    st.push(0);
    st.push(xs.len);
    while st.len > 0 {
        let hi = st.pop();
        let lo = st.pop();
        var l = lo;
        var h = hi;
        while h - l > 16 {
            let mid = l + (h - l) / 2;
            if F(xs[mid], xs[l]) { swap_at(xs, l, mid); }
            if F(xs[h - 1], xs[l]) { swap_at(xs, l, h - 1); }
            if F(xs[h - 1], xs[mid]) { swap_at(xs, mid, h - 1); }
            let p = xs[mid];
            var i = l;
            var j = h - 1;
            while i <= j {
                while F(xs[i], p) { i += 1; }
                while F(p, xs[j]) { j -= 1; }
                if i <= j {
                    swap_at(xs, i, j);
                    i += 1;
                    j -= 1;
                }
            }
            // [l, j] and [i, h): continue with the smaller, defer the larger.
            if j + 1 - l < h - i {
                st.push(i);
                st.push(h);
                h = j + 1;
            } else {
                st.push(l);
                st.push(j + 1);
                l = i;
            }
        }
        var i = l + 1;
        while i < h {
            let v = xs[i];
            var j = i - 1;
            while j >= l && F(v, xs[j]) {
                xs[j + 1] = xs[j];
                j -= 1;
            }
            xs[j + 1] = v;
            i += 1;
        }
    }
}

// Bottom-up merge sort; one temporary of xs.len elements on a fresh stack.
fn stable_sort<T>(xs: T[:]) { stable_sort(xs) { a, b => a < b } }
fn stable_sort<T, F>(xs: T[:]) {
    let n = xs.len;
    var tmp: T[>..] = [];
    tmp.append(xs);
    var width = 1;
    while width < n {
        var i = 0;
        while i < n {
            let mid = min(i + width, n);
            let hi = min(i + 2 * width, n);
            var a = i;
            var b = mid;
            var k = i;
            while a < mid && b < hi {
                if F(xs[b], xs[a]) { tmp[k] = xs[b]; b += 1; }
                else { tmp[k] = xs[a]; a += 1; }
                k += 1;
            }
            while a < mid { tmp[k] = xs[a]; a += 1; k += 1; }
            while b < hi { tmp[k] = xs[b]; b += 1; k += 1; }
            i += 2 * width;
        }
        copy_into(xs, tmp);
        width *= 2;
    }
}

// ASCII, in place.
fn to_lower(s: u8[:]) { for &c in s { if c >= 'A' && c <= 'Z' { c += 32; } } }
fn to_upper(s: u8[:]) { for &c in s { if c >= 'a' && c <= 'z' { c -= 32; } } }

// ---- arrays: changing the length -------------------------------------------------
// `xs: A&` binds the whole array, so each instantiates only for array kinds
// that have the operations it uses.

fn push_n<A, T>(xs: A&, v: T, n: i64) { for i in n { xs.push(v); } }

fn insert_at<A, T>(xs: A&, i: i64, v: T) {
    xs.push(v);
    var j = xs.len - 1;
    while j > i {
        xs[j] = xs[j - 1];
        j -= 1;
    }
    xs[i] = v;
}

// Removes and returns xs[i], shifting the rest down.
fn remove_at<A>(xs: A&, i: i64) {
    let v = xs[i];
    var j = i;
    while j + 1 < xs.len {
        xs[j] = xs[j + 1];
        j += 1;
    }
    xs.pop();
    return v;
}

// Removes and returns xs[i] in O(1), moving the last element into its place.
fn swap_remove<A>(xs: A&, i: i64) {
    let v = xs[i];
    xs[i] = xs[xs.len - 1];
    xs.pop();
    return v;
}

// Keeps the elements with F(x), in order.
fn retain<A, F>(xs: A&) {
    var w = 0;
    for x in xs {
        if F(x) {
            xs[w] = x;
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
            xs[w] = xs[i];
            w += 1;
        }
        i += 1;
    }
    xs.resize(w);
}

// A binary min-heap by < (or by F(a, b), "a goes before b").
fn heap_push<A, T>(xs: A&, v: T) { heap_push(xs, v) { a, b => a < b } }
fn heap_push<A, T, F>(xs: A&, v: T) {
    xs.push(v);
    var i = xs.len - 1;
    while i > 0 {
        let p = (i - 1) / 2;
        if F(xs[i], xs[p]) {
            swap_at(xs, i, p);
            i = p;
        } else {
            break;
        }
    }
}

fn sift_down<T, F>(xs: T[:], start: i64) {
    let n = xs.len;
    var i = start;
    loop {
        let l = 2 * i + 1;
        if l >= n { break; }
        var m = l;
        let r = l + 1;
        if r < n && F(xs[r], xs[l]) { m = r; }
        if F(xs[m], xs[i]) {
            swap_at(xs, i, m);
            i = m;
        } else {
            break;
        }
    }
}

fn heap_pop<A>(xs: A&) { heap_pop(xs) { a, b => a < b } }
fn heap_pop<A, F>(xs: A&) {
    let top = xs[0];
    let tail = xs.pop();
    if xs.len > 0 {
        xs[0] = tail;
        sift_down(xs, 0) { a, b => F(a, b) };
    }
    return top;
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

// v with a fixed number of decimals, rounded half up.
fn format_flt(out: u8[>..]&, v: f64, decimals: i64) {
    var x = v;
    if x < 0.0 {
        out.push('-');
        x = -x;
    }
    var scale = 1.0;
    for i in decimals { scale *= 10.0; }
    let scaled = (x * scale + 0.5) as i64;
    let iscale = scale as i64;
    format_uint(out, (scaled / iscale) as u64, 10, 0, ' ');
    if decimals > 0 {
        out.push('.');
        format_uint(out, (scaled % iscale) as u64, 10, decimals, '0');
    }
}

// An optionally signed whole-string integer; false on anything else.
fn parse_int(s: u8[:]) -> i64, bool { return parse_int(s, 10); }
fn parse_int(s: u8[:], base: i64) -> i64, bool {
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

// A decimal float with optional sign, fraction and exponent; false otherwise.
fn parse_flt(s: u8[:]) -> f64, bool {
    var i = 0;
    var neg = false;
    if s.len > 0 && (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
    }
    var v = 0.0;
    var digits = 0;
    while i < s.len && s[i] >= '0' && s[i] <= '9' {
        v = v * 10.0 + ((s[i] - '0') as f64);
        digits += 1;
        i += 1;
    }
    if i < s.len && s[i] == '.' {
        i += 1;
        var scale = 0.1;
        while i < s.len && s[i] >= '0' && s[i] <= '9' {
            v += ((s[i] - '0') as f64) * scale;
            scale *= 0.1;
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
            e = e * 10 + ((s[i] - '0') as i64);
            edigits += 1;
            i += 1;
        }
        if edigits == 0 { return 0.0, false; }
        var m = 1.0;
        for k in e { m *= 10.0; }
        if eneg { v /= m; } else { v *= m; }
    }
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

// The parts between separators, as slices into s; empty parts are kept.
fn split(s: u8[:], sep: u8) -> u8[:][>..] {
    var out: u8[:][>..] = [];
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
