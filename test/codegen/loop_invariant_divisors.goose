// Division and remainder by a variable a loop cannot change (§6.2) run as a
// multiply by a number computed before the loop: every unsigned width,
// signed operands the analysis knows to be nonnegative, divisors of every
// shape (one, powers of two, odd, near the top of the range), nested loops
// sharing one divisor, a divisor bound in an outer loop's body, a parameter
// a tail call rebinds, and a zero divisor, which aborts at its division only
// when a division runs.
import std;

fn mix(x: u64) -> u64 { return (x ^ (x >> 29)) * 0xBF58476D1CE4E5B9; }

// A checksum of x / d and x % d over values spread across the range.
fn sweep64(d: u64) -> u64 {
    var s: u64 = 0;
    var x: u64 = 12345;
    for i in 2000 {
        x = mix(x + i as! u64);
        s = s * 31 + x / d + (x % d) * 7;
        s = s * 31 + (x >> 40) / d + (x >> 40) % d;
    }
    s += 0xFFFFFFFFFFFFFFFF / d + 0xFFFFFFFFFFFFFFFF % d;
    return s;
}

fn sweep32(d: u32) -> u32 {
    var s: u32 = 0;
    var x: u64 = 777;
    for i in 1000 {
        x = mix(x + i as! u64);
        let v = (x >> 32) as! u32;
        s = s * 31 + v / d + v % d;
    }
    return s;
}

fn sweep8(d: u8) -> u32 {
    var s: u32 = 0;
    for v: u8 in 0..256 { s = s * 7 + (v / d) + (v % d); }
    return s;
}

// Signed, with both operands known nonnegative.
fn sweep_signed(n: i64, d: i64) -> i64 {
    var s = 0;
    if d >= 1 {
        for i in n { s += i / d + i % d; }
    }
    return s;
}

// One divisor in an inner loop and an outer one; another bound per outer
// iteration.
fn nested(n: u64) -> u64 {
    var s: u64 = 0;
    for i in 20 {
        let e = (i + 3) as! u64;
        for j in 50 { s += (j as! u64 * 7919) / n + (j as! u64 * 104729) % e; }
        s += (i as! u64 * 1000003) % n;
    }
    return s;
}

// A loop's own binders change every iteration: no divisor of its.
fn own_binder(n: u64) -> u64 {
    var s: u64 = 0;
    for d: u64 in 1..n { s += 1000003 / d + 1000003 % d; }
    let xs: u64[] = [3, 5, 7];
    for d, i in xs { s += 100 / d + (i as! u64 + 50) % d; }
    return s;
}

// Tail-recursion elimination turns the self-call into a loop that assigns
// the parameters each round, the divisor among them.
recursive fn leading(n: u64, base: u64) -> u64 {
    let q = n / base;
    if q == 0 { return n; }
    return leading(q, base + 1);
}

fn by_zero(d: u64, runs: i64) -> u64 {
    var s: u64 = 0;
    for i in runs { s += 100 / d; }
    return s;
}

fn main() {
    let divisors: u64[] = [1, 2, 3, 5, 7, 10, 16, 25, 60, 641, 1000, 4096, 65537, 1000000007,
                           0x80000000, 0xFFFFFFFF, 0x100000001, 6700417 * 641,
                           0x7FFFFFFFFFFFFFFF, 0x8000000000000000, 0xFFFFFFFFFFFFFFFE,
                           0xFFFFFFFFFFFFFFFF];
    for d in divisors { print(d, " ", sweep64(d)); }
    let d32: u32[] = [1, 3, 7, 255, 65536, 2147483648, 4294967295];
    for d in d32 { print(d, " ", sweep32(d)); }
    print(sweep8(1), " ", sweep8(3), " ", sweep8(128), " ", sweep8(255));
    print(sweep_signed(1000, 1), " ", sweep_signed(1000, 9), " ", sweep_signed(1000, 0));
    print(nested(13), " ", nested(1), " ", nested(1 << 20));
    print(own_binder(100), " ", by_zero(0, 0), " ", by_zero(7, 3));
    print(leading(1000000, 2), " ", leading(255, 16), " ", leading(1000000000000000000, 3));
    print(by_zero(0, 1));
}
