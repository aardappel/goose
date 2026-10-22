// Compound division and remainder (`/=`, `%=`) by a variable a loop cannot
// change multiply by a number computed before the loop, as `/` and `%` do
// (§6.2): targets of every unsigned width, one written through a reference
// and two in fields, a divisor a `/` in the same loop divides by too, and a
// converted one. Each update is held to the same one by a divisor read from
// an array, which no loop fixes. A signed target divides by the divisor
// itself, and a zero divisor aborts at its division only when one runs.
import std;

var mismatches = 0;

fn check(ok: bool) { if !ok { mismatches += 1; } }

fn mix(x: u64) -> u64 { return (x ^ (x >> 29)) * 0xBF58476D1CE4E5B9; }

// dv[0] is d, here and below.
fn wide(d: u64, dv: u64[:]) -> u64 {
    var s: u64 = 0;
    var x: u64 = 12345;
    for i in 2000 {
        x = mix(x + i as! u64);
        var q = x;
        q /= d;
        var r = x;
        r %= d;
        check(q == x / dv[0] && r == x % dv[0]);
        s = s * 31 + q + r;
    }
    return s;
}

fn narrow32(d: u32, dv: u32[:]) -> u32 {
    var s: u32 = 0;
    var x: u64 = 777;
    for i in 2000 {
        x = mix(x + i as! u64);
        let v = (x >> 32) as! u32;
        var q = v;
        q /= d;
        var r = v;
        r %= d;
        check(q == v / dv[0] && r == v % dv[0]);
        s = s * 31 + q + r;
    }
    return s;
}

fn narrow16(d: u16, dv: u16[:]) -> u32 {
    var s: u32 = 0;
    for v: u16 in 0..65536 {
        var q = v;
        q /= d;
        var r = v;
        r %= d;
        check(q == v / dv[0] && r == v % dv[0]);
        s = s * 7 + q + r;
    }
    return s;
}

fn narrow8(d: u8, dv: u8[:]) -> u32 {
    var s: u32 = 0;
    for v: u8 in 0..256 {
        var q = v;
        q /= d;
        var r = v;
        r %= d;
        check(q == v / dv[0] && r == v % dv[0]);
        s = s * 7 + q + r;
    }
    return s;
}

// Each element reduced in place, through the loop's reference to it.
fn reduce(xs: u32[>..]&, m: u32) {
    for &x in xs { x %= m; }
}

struct Acc { q: u64, r: u32 }

// Fields as targets: one by a divisor a `/` in the loop divides by as well,
// and one by a conversion, which is checked. kv[0] is k.
fn fields(d: u32, k: i64, dv: u32[:], kv: u64[:]) -> u64 {
    var a = Acc { q: 0xFFFFFFFFFFFFFFFF, r: 0 };
    var mq: u64 = 0xFFFFFFFFFFFFFFFF;
    var s: u64 = 0;
    for i in 70 {
        let v = (i as! u32) * 2654435761;
        a.r = v;
        a.r %= d;
        let quot = v / d;
        a.q /= k as u64;
        mq = mq / kv[0];
        check(a.r == v % dv[0] && quot == v / dv[0] && a.q == mq);
        s = s * 31 + a.q + a.r + quot;
        if a.q == 0 {
            a.q = 0xFFFFFFFFFFFFFFFF - i as! u64;
            mq = a.q;
        }
    }
    return s;
}

// Signed: BCE judges no compound assignment's operands nonnegative.
fn signed(n: i64, d: i64, dv: i64[:]) -> i64 {
    var s = 0;
    for i in n {
        var q = i * 7919 - 50000;
        q /= d;
        var r = i * 104729 - 50000;
        r %= d;
        check(q == (i * 7919 - 50000) / dv[0] && r == (i * 104729 - 50000) % dv[0]);
        s = s * 31 + q + r;
    }
    return s;
}

fn by_zero(d: u32, runs: i64) -> u32 {
    var s: u32 = 100;
    for i in runs { s %= d; }
    return s;
}

fn main() {
    let d64: u64[] = [1, 2, 3, 7, 10, 641, 65537, 1000000007, 0x80000000, 0xFFFFFFFF,
                      0x100000001, 0x7FFFFFFFFFFFFFFF, 0x8000000000000000, 0xFFFFFFFFFFFFFFFF];
    for d in d64 {
        var dv: u64[>..] = [];
        dv.push(d);
        print(d, " ", wide(d, dv));
    }
    let d32: u32[] = [1, 3, 7, 255, 65536, 2147483648, 4294967295];
    for d in d32 {
        var dv: u32[>..] = [];
        dv.push(d);
        var kv: u64[>..] = [];
        kv.push(d);
        let k: i64 = d;
        print(d, " ", narrow32(d, dv), " ", fields(d, k, dv, kv));
    }
    let d16: u16[] = [1, 3, 7, 255, 256, 32769, 65535];
    for d in d16 {
        var dv: u16[>..] = [];
        dv.push(d);
        print(d, " ", narrow16(d, dv));
    }
    let d8: u8[] = [1, 3, 7, 128, 255];
    for d in d8 {
        var dv: u8[>..] = [];
        dv.push(d);
        print(d, " ", narrow8(d, dv));
    }
    var xs: u32[>..] = [];
    for i in 1000 { xs.push((i as! u32) * 2654435761); }
    let ys = xs;
    let m = (xs.len as! u32) * 1000 + 3;
    reduce(xs, m);
    for x, i in xs { check(x == ys[i] % 1000003); }
    print(xs[999], " ", xs[1]);
    let ds: i64[] = [1, 7, -7, 1000];
    for d in ds {
        var dv: i64[>..] = [];
        dv.push(d);
        print(d, " ", signed(30, d, dv));
    }
    print("mismatches ", mismatches);
    print(by_zero(0, 0), " ", by_zero(7, 3));
    print(by_zero(0, 1));
}
