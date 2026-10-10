// Loops codegen restates for the C compiler (optimize_loops.h, and BCE's
// lenbound): strip-mined loops that take their counter modulo a constant,
// in-order float sums whose terms are computed a block at a time, loops over
// limited arrays bounded by the capacity, and `while v > 0 { v--; ... }`
// countdowns run as counted loops. Each shape_ function is checked against
// a reference written so that none of them applies, over lengths around
// every block size, with breaks, continues and returns inside. The runner
// also checks that the shape_ functions' C has the restated loops, and that
// the keep_ functions', whose loops look alike but must not be restated,
// has none (run_tests.py, check_loop_shapes).

import std;

// --- strip-mining: `i % K` ----------------------------------------------------

fn shape_strip_lanes(data: u8[:]) -> u32 {
    var lanes: u32[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    for byte, i in data {
        let lane = i % 8;
        var h = lanes[lane];
        h = (h << 5) + h + byte;
        lanes[lane] = h ^ (h >> 3);
    }
    var r: u32 = 0;
    for h in lanes { r = r * 31 + h; }
    return r;
}

fn ref_lanes(data: u8[:]) -> u32 {
    var lanes: u32[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    var lane = 0;
    var k = 0;
    while k < data.len {
        var h = lanes[lane];
        h = (h << 5) + h + data[k];
        lanes[lane] = h ^ (h >> 3);
        lane = if lane == 7 { 0 } else { lane + 1 };
        k++;
    }
    var r: u32 = 0;
    for h in lanes { r = r * 31 + h; }
    return r;
}

// A count loop with continue and break, the modulus used twice.
fn shape_strip_count(n: i64, limit: i64) -> i64 {
    var s = 0;
    for i in n {
        if i % 4 == 1 { continue; }
        s += i * (i % 4 + 1);
        if s > limit { break; }
    }
    return s;
}

fn ref_count(n: i64, limit: i64) -> i64 {
    var s = 0;
    var i = 0;
    var m = 0;
    while i < n {
        let cur = m;
        i++;
        m = if m == 3 { 0 } else { m + 1 };
        if cur == 1 { continue; }
        s += (i - 1) * (cur + 1);
        if s > limit { break; }
    }
    return s;
}

// A 32-bit range counter and a modulus that is not a power of two.
fn shape_strip_range(n: i32) -> i64 {
    var acc: i64[3] = [0; 3];
    for i in 0..n { acc[i % 3] += i as i64 * 7 + 1; }
    return acc[0] * 1000000 + acc[1] * 1000 + acc[2];
}

fn ref_range(n: i32) -> i64 {
    var acc: i64[3] = [0; 3];
    var i: i32 = 0;
    var m = 0;
    while i < n {
        acc[m] += i as i64 * 7 + 1;
        m = if m == 2 { 0 } else { m + 1 };
        i++;
    }
    return acc[0] * 1000000 + acc[1] * 1000 + acc[2];
}

// An 8-bit range counter, K = 16.
fn shape_strip_u8(n: u8) -> i64 {
    var acc: i64[16] = [0; 16];
    for i in 0..n { acc[i % 16] += i; }
    var r = 0;
    for a, k in acc { r += a * (k + 1); }
    return r;
}

fn ref_u8(n: u8) -> i64 {
    var acc: i64[16] = [0; 16];
    var i = 0;
    while i < n {
        acc[i & 15] += i;
        i++;
    }
    var r = 0;
    for a, k in acc { r += a * (k + 1); }
    return r;
}

// Returns from inside the loop, and runs a loop of its own in the body.
fn shape_strip_return(xs: i64[:]) -> i64 {
    var acc: i64[4] = [0; 4];
    for x, i in xs {
        if x < 0 { return i * 100 + i % 4 + acc[0] + acc[3]; }
        for j in i % 4 { acc[j] += x; }
        acc[i % 4] += 1;
    }
    return acc[0] + acc[1] * 10 + acc[2] * 100 + acc[3] * 1000;
}

fn ref_return(xs: i64[:]) -> i64 {
    var acc: i64[4] = [0; 4];
    var i = 0;
    while i < xs.len {
        let x = xs[i];
        let m = i - (i / 4) * 4;
        if x < 0 { return i * 100 + m + acc[0] + acc[3]; }
        var j = 0;
        while j < m {
            acc[j] += x;
            j++;
        }
        acc[m] += 1;
        i++;
    }
    return acc[0] + acc[1] * 10 + acc[2] * 100 + acc[3] * 1000;
}

// Struct elements and struct locals, which each copy of the body declares.
struct Pair { a: u32, b: u32 }

fn mix(p: Pair, lane: i64) -> Pair { Pair { a: p.a * 3 + lane as u32, b: p.b ^ p.a } }

fn shape_strip_structs(ps: Pair[:]) -> u32 {
    var lanes: Pair[2] = [Pair { a: 1, b: 2 }; 2];
    for p, i in ps {
        let q = mix(p, i % 2);
        let old = lanes[i % 2];
        lanes[i % 2] = Pair { a: old.a + q.a, b: old.b ^ q.b };
    }
    return lanes[0].a * 7 + lanes[0].b + lanes[1].a * 13 + lanes[1].b;
}

fn ref_structs(ps: Pair[:]) -> u32 {
    var lanes: Pair[2] = [Pair { a: 1, b: 2 }; 2];
    var i = 0;
    while i < ps.len {
        let lane = i & 1;
        let q = mix(ps[i], lane);
        let old = lanes[lane];
        lanes[lane] = Pair { a: old.a + q.a, b: old.b ^ q.b };
        i++;
    }
    return lanes[0].a * 7 + lanes[0].b + lanes[1].a * 13 + lanes[1].b;
}

// A fixed array behind a reference, and two moduli: only those by the first
// one found read the inner counter, the others still compute.
fn shape_strip_fixed(m: i64[37]&) -> i64 {
    var s = 0;
    for x, i in m { s += x * (i % 4) + i % 3; }
    return s;
}

fn ref_fixed(m: i64[37]&) -> i64 {
    var s = 0;
    var i = 0;
    while i < 37 {
        s += m[i] * (i - (i / 4) * 4) + (i - (i / 3) * 3);
        i++;
    }
    return s;
}

// --- in-order float sums --------------------------------------------------------

fn shape_sum_dot(x: f64[:], y: f64[:]) -> f64 {
    var s = 0.0;
    for a, k in x { s += a * y[k]; }
    return s;
}

fn ref_dot(x: f64[:], y: f64[:]) -> f64 {
    var s = 0.0;
    var k = 0;
    while k < x.len {
        s += x[k] * y[k];
        k++;
    }
    return s;
}

fn term(i: i64, j: i64) -> f64 {
    let ij = (i + j) as f64;
    return 1.0 / (ij * (ij + 1.0) / 2.0 + i + 1.0);
}

fn shape_sum_count(i: i64, n: i64) -> f64 {
    var s = 0.1;
    for j in n { s += term(i, j) * 3.0; }
    return s;
}

fn ref_sum_count(i: i64, n: i64) -> f64 {
    var s = 0.1;
    var j = 0;
    while j < n {
        s += term(i, j) * 3.0;
        j++;
    }
    return s;
}

fn shape_sum_f32(xs: f32[:]) -> f32 {
    var s: f32 = 0.0;
    for x in xs { s += x * 1.5 + 0.25; }
    return s;
}

fn ref_sum_f32(xs: f32[:]) -> f32 {
    var s: f32 = 0.0;
    var k = 0;
    while k < xs.len {
        s += xs[k] * 1.5 + 0.25;
        k++;
    }
    return s;
}

// A range from any start, near the ends of i64 too: the count of iterations
// left is taken without overflowing.
fn shape_sum_range(lo: i64, hi: i64) -> f64 {
    var s = 0.0;
    for j in lo..hi { s += (j % 1000) / 7.0; }
    return s;
}

fn ref_sum_range(lo: i64, hi: i64) -> f64 {
    var s = 0.0;
    if lo >= hi { return s; }
    var j = lo;
    loop {
        s += (j % 1000) / 7.0;
        if j == hi - 1 { break; }
        j++;
    }
    return s;
}

// The term reads the sum: not restated, and the result shows if it were.
fn keep_sum_reads(xs: f64[:]) -> f64 {
    var s = 1.0;
    for x in xs { s += s * 0.5 + x; }
    return s;
}

fn ref_sum_reads(xs: f64[:]) -> f64 {
    var s = 1.0;
    var k = 0;
    while k < xs.len {
        s += s * 0.5 + xs[k];
        k++;
    }
    return s;
}

// --- loops over limited arrays ----------------------------------------------------

struct Node { v: i64, links: i64[..8] }

fn shape_bound_count(n: Node&) -> i64 { count(n.links) { it > 2 } }
fn shape_bound_any(n: Node&) -> bool { any(n.links) { it == 5 } }
fn shape_bound_all(n: Node&) -> bool { all(n.links) { it < 7 } }

fn shape_bound_for(l: i64[..6]&) -> i64 {
    var s = 0;
    for x, i in l {
        if x == 99 { break; }
        if x == 50 { continue; }
        s += x * (i + 1);
    }
    return s;
}

fn ref_bound_for(l: i64[..6]&) -> i64 {
    var s = 0;
    var i = 0;
    while i < l.len {
        let x = l[i];
        i++;
        if x == 99 { break; }
        if x == 50 { continue; }
        s += x * i;
    }
    return s;
}

// Growth during the walk is seen, as the length is still tested every time.
fn shape_bound_grow(l: i64[..8]&) -> i64 {
    var s = 0;
    for x in l {
        if l.len < 8 { l.push(x * 2 + 1); }
        s = s * 3 + x;
    }
    return s;
}

// --- countdowns -------------------------------------------------------------------

fn shape_down(n: i64) -> i64 {
    var i = n;
    var s = 0;
    while i > 0 {
        i--;
        s = s * 3 + i;
    }
    return s * 1000 + i;
}

fn ref_down(n: i64) -> i64 {
    var s = 0;
    var k = n - 1;
    while k >= 0 {
        s = s * 3 + k;
        k -= 1;
    }
    return s * 1000 + (if n > 0 { 0 } else { n });
}

fn shape_down_u8(n: u8) -> i64 {
    var i = n;
    var s = 0;
    while 0 < i {
        i -= 1;
        let w: i64 = i;
        s += w * (w + 1);
    }
    return s + i;
}

fn shape_down_i8(n: i8) -> i64 {
    var i = n;
    var s = 0;
    while i > 0 {
        i--;
        s += i;
    }
    return s * 10 + i;
}

fn shape_down_u64(n: u64) -> u64 {
    var i = n;
    var s: u64 = 0;
    while i > 0 {
        i--;
        s = s * 5 + i;
    }
    return s + i;
}

// continue and break: the counter at the break is the decremented one.
fn shape_down_jumps(n: i64) -> i64 {
    var i = n;
    var s = 0;
    while i > 0 {
        i--;
        if i % 3 == 0 { continue; }
        if i == 4 { break; }
        s = s * 7 + i;
    }
    return s * 100 + i;
}

fn ref_down_jumps(n: i64) -> i64 {
    var s = 0;
    var k = n;
    var last = n;
    loop {
        if k <= 0 { break; }
        k -= 1;
        last = k;
        if k % 3 == 0 { continue; }
        if k == 4 { break; }
        s = s * 7 + k;
    }
    return s * 100 + last;
}

// The body steps the counter again: not restated.
fn keep_down_twice(n: i64) -> i64 {
    var i = n;
    var s = 0;
    while i > 0 {
        i--;
        if i % 2 == 0 && i > 0 { i--; }
        s = s * 2 + i;
    }
    return s * 10 + i;
}

fn ref_down_twice(n: i64) -> i64 {
    var k = n;
    var s = 0;
    loop {
        if !(k > 0) { break; }
        k = k - 1;
        if k % 2 == 0 && k > 0 { k = k - 1; }
        s = s * 2 + k;
    }
    return s * 10 + k;
}

// --- driver -----------------------------------------------------------------------

fn check(what: const u8[:], n: i64, ok: bool) {
    if !ok { print("MISMATCH ", what, " at ", n); }
}

// More calls of the small shape_ and keep_ functions, so that -O1 keeps each
// a function of its own, whose C the runner reads.
fn again(xs: f64[:], ys: f64[:], fs: f32[:], m: i64[37]&, node: Node&) {
    print(shape_sum_dot(xs, ys) + shape_sum_dot(ys, xs) + shape_sum_dot(xs, xs) +
          shape_sum_dot(ys, ys) + shape_sum_dot(ys[1..], xs));
    print(shape_sum_f32(fs) + shape_sum_f32(fs[1..]) + shape_sum_f32(fs[2..]) +
          shape_sum_f32(fs[3..]) + shape_sum_f32(fs[4..]));
    print(keep_sum_reads(xs) + keep_sum_reads(ys) + keep_sum_reads(xs[1..]) +
          keep_sum_reads(ys[1..]) + keep_sum_reads(xs[2..]));
    print(shape_strip_fixed(m) + shape_strip_fixed(m) * 2 + shape_strip_fixed(m) * 3 +
          shape_strip_fixed(m) * 4 + shape_strip_fixed(m) * 5);
    print(shape_bound_count(node) + shape_bound_count(node) * 2 + shape_bound_count(node) * 3 +
          shape_bound_count(node) * 4 + shape_bound_count(node) * 5 + shape_bound_count(node) * 6);
    print(shape_bound_any(node) && shape_bound_any(node) && shape_bound_any(node) &&
          shape_bound_any(node) && shape_bound_any(node) && shape_bound_any(node));
    print(shape_bound_all(node) || shape_bound_all(node) || shape_bound_all(node) ||
          shape_bound_all(node) || shape_bound_all(node) || shape_bound_all(node));
    print(shape_down_i8(1) + shape_down_i8(2) + shape_down_i8(3) + shape_down_i8(4));
    print(shape_down_u64(1) + shape_down_u64(2) + shape_down_u64(3) + shape_down_u64(4));
}

fn main() {
    var bytes: u8[>..] = [];
    var longs: i64[>..] = [];
    var ps: Pair[>..] = [];
    var xs: f64[>..] = [];
    var ys: f64[>..] = [];
    var fs: f32[>..] = [];
    for n in 40 {
        bytes.clear();
        longs.clear();
        ps.clear();
        xs.clear();
        ys.clear();
        fs.clear();
        for k in n {
            bytes.push((k * 37 + 11) as! u8);
            longs.push(k * 5 + 3);
            ps.push(Pair { a: (k * 7) as! u32, b: (k * 13 + 1) as! u32 });
            xs.push(k * 0.37 + 0.1);
            ys.push(1.0 / (k + 3.0));
            fs.push(k * 0.3);
        }
        check("lanes", n, shape_strip_lanes(bytes) == ref_lanes(bytes));
        check("count", n, shape_strip_count(n - 3, 1000000) == ref_count(n - 3, 1000000));
        check("count break", n, shape_strip_count(n, 40) == ref_count(n, 40));
        check("range", n, shape_strip_range(n as i32 - 2) == ref_range(n as i32 - 2));
        check("u8", n, shape_strip_u8(n as! u8) == ref_u8(n as! u8));
        check("return", n, shape_strip_return(longs) == ref_return(longs));
        if n > 0 {
            longs[n / 2] = -1;
            check("return early", n, shape_strip_return(longs) == ref_return(longs));
        }
        check("structs", n, shape_strip_structs(ps) == ref_structs(ps));
        check("dot", n, shape_sum_dot(xs, ys) == ref_dot(xs, ys));
        check("sum count", n, shape_sum_count(n, n) == ref_sum_count(n, n));
        check("f32", n, shape_sum_f32(fs) == ref_sum_f32(fs));
        check("range sum", n, shape_sum_range(-n, n * 2 / 3) == ref_sum_range(-n, n * 2 / 3));
        check("reads", n, keep_sum_reads(xs) == ref_sum_reads(xs));
        check("down", n, shape_down(n - 2) == ref_down(n - 2));
        check("down jumps", n, shape_down_jumps(n - 2) == ref_down_jumps(n - 2));
        check("down twice", n, keep_down_twice(n - 2) == ref_down_twice(n - 2));
    }
    print(shape_strip_lanes(bytes), " ", shape_strip_count(39, 1000), " ", shape_strip_range(39),
          " ", shape_strip_u8(255), " ", shape_strip_return(longs), " ", shape_strip_structs(ps));
    print(shape_sum_dot(xs, ys), " ", shape_sum_count(5, 39), " ", shape_sum_f32(fs));
    print(shape_sum_range(9223372036854775800, 9223372036854775807), " ",
          shape_sum_range(-9223372036854775807 - 1, -9223372036854775807 + 12), " ",
          shape_sum_range(5, 3));
    check("range top", 0, shape_sum_range(9223372036854775800, 9223372036854775807) ==
          ref_sum_range(9223372036854775800, 9223372036854775807));
    check("range bottom", 0, shape_sum_range(-9223372036854775807 - 1, -9223372036854775807 + 12) ==
          ref_sum_range(-9223372036854775807 - 1, -9223372036854775807 + 12));
    var m: i64[37] = [0; 37];
    for &x, i in m { x = i * i - 5; }
    print(shape_strip_fixed(m), " ", ref_fixed(m));

    var node = Node { v: 0, links: [] };
    for n in 9 {
        print("node ", n, ": ", shape_bound_count(node), " ", shape_bound_any(node), " ",
              shape_bound_all(node));
        if n < 8 { node.links.push(n); }
    }
    var l: i64[..6] = [];
    for n in 7 {
        check("bound for", n, shape_bound_for(l) == ref_bound_for(l));
        if n < 6 { l.push(if n == 2 { 50 } else { n * 4 + 1 }); }
    }
    l[4] = 99;
    print(shape_bound_for(l), " ", ref_bound_for(l));
    var g: i64[..8] = [];
    print(shape_bound_grow(g));
    g.push(1);
    print(shape_bound_grow(g), " ", g.len);
    g.clear();
    g.push(3);
    g.push(4);
    print(shape_bound_grow(g), " ", g.len);

    print(shape_down(-3), " ", shape_down(0), " ", shape_down(1), " ", shape_down(9));
    print(shape_down_u8(0), " ", shape_down_u8(255), " ", shape_down_i8(127), " ",
          shape_down_i8(-128), " ", shape_down_u64(0), " ", shape_down_u64(6));
    print(shape_down_jumps(20), " ", keep_down_twice(20));
    again(xs, ys, fs, m, node);
}
