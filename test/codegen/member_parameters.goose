// A slice, a reference to a resizable value and a reusable pool's reference
// are passed as their members, a C parameter each (C.3): through calls with
// more of them than a convention has registers for, a recursion pushing
// through its reference while it walks its slices, a case-function dispatch
// sharing them between its arms, nested functions capturing a pool, a pool
// reference and a slice parameter, a body rebinding its slice parameter, a
// simd function's versions, a format overload, and an exported function's C
// wrapper. Returning two values keeps a function out of line. run_tests.py
// reads the -O1 C of walk.
struct Cell { v: i64 }
enum Op { Add, Scale { k: i64 } }

fn mix(a: i64[:], out: i64[>..]&, name: const u8[:], log: u8[>..]&, b: i64[:], k: i64,
       more: i64[>..]&) -> i64, i64 {
    var s = k;
    for x in a { out.push(x * 2); s += x; }
    for x in b { more.push(x + k); s += x; }
    log.append(name);
    log.push('|');
    return s, out.len + more.len;
}

recursive fn walk(xs: i64[:], depth: i64, out: i64[>..]&, tag: const u8[:]) {
    if xs.len == 0 { return; }
    out.push(xs[0] * 10 + depth + tag.len);
    walk(xs[1..], depth + 1, out, tag);
    out.push(-xs[0]);
}

fn apply(op: Op.Add, xs: i64[:], out: i64[>..]&) -> i64 {
    for x in xs { out.push(x + 1); }
    return out.len;
}
fn apply(op: Op.Scale, xs: i64[:], out: i64[>..]&) -> i64 {
    for x in xs { out.push(x * op.k); }
    return -out.len;
}

fn fill(pool: Cell[>..]&, xs: i64[:], tag: const u8[:], log: u8[>..]&, n: i64) -> i64, i64 {
    fn one(v: i64) -> i64, bool {
        let i = pool.alloc_index(Cell { v: v });
        if v < 0 { pool.free(i); }
        return i, v < 0;
    }
    var last = 0;
    for x in xs {
        let i, freed = one(x * n);
        if !freed { last = i; }
    }
    for k in 2 {
        let i, freed = one(-1 - k);
        if freed { last += i; }
    }
    log.append(tag);
    return last, pool.len;
}

// A nested function reaching a slice parameter as a free variable, and a
// body that rebinds and slices it.
fn sum_tail(var xs: i64[:], skip: i64) -> i64 {
    fn at(i: i64) -> i64 { xs[i] }
    var t = 0;
    for i in xs.len { t += at(i); }
    xs = xs[skip..];
    for x in xs { t += x; }
    return t;
}

// Every version of a simd function takes the members.
simd fn simd_sum(xs: const i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    return t;
}

struct Range { lo: i64, hi: i64 }
fn format(out: u8[>..]&, r: Range) { format(out, "[", r.lo, "..", r.hi, ")"); }

export "member_sum" fn sum(xs: const i64[:], k: i64) -> i64 {
    var s = 0;
    for x in xs { s += x * k; }
    return s;
}

fn main() {
    var out: i64[>..] = [];
    var more: i64[>..] = [];
    var log: u8[>..] = [];
    let a: i64[] = [1, 2, 3];
    let b: i64[] = [10, 20];
    let s1, n1 = mix(a, out, "first", log, b, 5, more);
    print(s1, " ", n1, " ", out, " ", more, " ", log);
    let s2, n2 = mix(b[1..], more, "second", log, a[..2], -1, out);
    print(s2, " ", n2, " ", out, " ", more, " ", log);

    var trail: i64[>..] = [];
    walk(a, 0, trail, "ab");
    print(trail);

    var applied: i64[>..] = [];
    for op in [Op.Add, Op.Scale { k: 3 }, Op.Add] { print(apply(op, a, applied)); }
    print(applied);

    reusable var cells: Cell[>..] = [];
    let last, len = fill(cells, b, "cells", log, 2);
    print(last, " ", len, " ", cells[1].v, " ", log);
    fn grab(v: i64) -> i64, i64 {
        let i = cells.alloc_index(Cell { v: v });
        return i, cells.len;
    }
    let gi, glen = grab(7);
    print(gi, " ", glen, " ", cells[gi].v);

    print(Range { lo: 2, hi: 7 }, " ", sum(a, 3), " ", sum(b[..1], 2));
    print(sum_tail(a, 1), " ", simd_sum(b), " ", simd_sum(a[1..]));
}
