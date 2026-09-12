// Arbitrary-precision integers on an array of 32-bit limbs, least
// significant first. Multiplying two limbs needs a 64-bit product and a
// carry, which is exactly the arithmetic Goose's typed integers express:
// `u32 * u32` is a 32-bit multiply, so the code says `as u64` where it
// wants the wide one, and the carries are visible instead of accidental.
// Computes 100!, the 500th Fibonacci number and 2^1000, and prints them in
// decimal with repeated division.
//
// Build and run (from this directory):
//   goose -o bignum.c 08_bignum.goose && cl bignum.c && bignum

import std;

let BASE: u64 = 4294967296;              // 2^32

// A number is its limbs; the value 0 is an empty array. Grow-only is
// enough: normalizing pops leading zero limbs, which a grow-only array
// allows wherever nothing in scope refers into it, and nothing ever points
// into a number. Fresh results are built into whatever receives them, so
// `let f = factorial(100)` allocates nothing on the way.
fn from_int(v: u64) -> u32[>..] {
    var n: u32[>..] = [];
    var x = v;
    while x > 0 { n.push(x as! u32); x = x >> 32; }
    return n;
}

// n = n * m + a, in place: one pass over the limbs with a running carry.
fn mul_add_small(n: u32[>..]&, m: u32, a: u32) {
    var carry = a as u64;
    for &limb in n {
        let t = limb as u64 * m as u64 + carry;   // a 64-bit multiply; u32 * u32 would wrap at 32
        limb = t as! u32;                // the low half stays, the rest carries
        carry = t >> 32;
    }
    if carry > 0 { n.push(carry as! u32); }
}

fn add(a: u32[:], b: u32[:]) -> u32[>..] {
    var r: u32[>..] = [];
    var carry: u64 = 0;
    for i in max(a.len, b.len) {
        let x: u64 = if i < a.len { a[i] } else { 0 };
        let y: u64 = if i < b.len { b[i] } else { 0 };
        let t = x + y + carry;
        r.push(t as! u32);
        carry = t >> 32;
    }
    if carry > 0 { r.push(carry as! u32); }
    return r;
}

// Schoolbook multiplication: r[i + j] accumulates a[i] * b[j].
fn mul(a: u32[:], b: u32[:]) -> u32[>..] {
    var r: u32[>..] = [];
    r.resize(a.len + b.len, 0);
    for i in a.len {
        var carry: u64 = 0;
        for j in b.len {
            let t = r[i + j] as u64 + a[i] as u64 * b[j] as u64 + carry;
            r[i + j] = t as! u32;
            carry = t >> 32;
        }
        r[i + b.len] = carry as! u32;
    }
    while r.len > 0 && r[r.len - 1] == 0 { r.pop(); }   // drop leading zeros
    return r;
}

// n / d in place, returning the remainder: the step decimal printing needs.
// The pop through the reference is checked at each call, against what the
// caller has in scope.
fn div_small(n: u32[>..]&, d: u32) -> u32 {
    var rem: u64 = 0;
    var i = n.len;
    while i > 0 {
        i--;
        let cur = rem << 32 | n[i] as u64;
        n[i] = (cur / d as u64) as! u32;
        rem = cur % d as u64;
    }
    while n.len > 0 && n[n.len - 1] == 0 { n.pop(); }
    return rem as! u32;
}

// Decimal text: peel off nine digits at a time from a working copy, then
// reverse the chunks into the output.
fn format(out: u8[>..]&, n: u32[>..]&) {
    if n.len == 0 { out.push('0'); return; }
    var work = copy(n);                  // the one copy, since division consumes its input
    var chunks: u32[>..] = [];           // nine decimal digits per entry, freed with the call
    while work.len > 0 {
        let chunk = div_small(work, 1000000000);
        chunks.push(chunk);
    }
    var first = true;
    each_rev(chunks) { c =>
        if first { format(out, c); first = false; }
        else { format_uint(out, c as u64, 10, 9, '0'); }
    };
}

fn factorial(k: i64) -> u32[>..] {
    var n = from_int(1);
    for i in 2..k + 1 { mul_add_small(n, i as u32, 0); }
    return n;
}

fn fib(k: i64) -> u32[>..] {
    var a = from_int(0);
    var b = from_int(1);
    for i in k {
        let c = add(a, b);
        a = copy(b);
        b = copy(c);
    }
    return a;
}

fn pow2(k: i64) -> u32[>..] {
    var n = from_int(1);
    for i in k { mul_add_small(n, 2, 0); }
    return n;
}

fn digits(n: u32[>..]&) -> i64 { str(n).len }

fn main() {
    let f100 = factorial(100);
    print("100! = ", f100);
    print("  ", f100.len, " limbs, ", digits(f100), " digits");
    let f500 = fib(500);
    print("fib(500) = ", f500);
    let p = pow2(1000);
    print("2^1000 has ", digits(p), " digits and ends in ...", str(p)[^12..]);
    // (2^500)^2 == 2^1000, through the general multiply.
    let h = pow2(500);
    let sq = mul(h, h);
    print("2^500 squared matches: ", sq == p);
    // 100! / (99 * 100) == 98!
    var q = copy(f100);
    div_small(q, 99);
    div_small(q, 100);
    print("100! / 9900 == 98!: ", q == factorial(98));
}
