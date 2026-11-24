// runtime-debug: std's bit helpers -- rotations, byte swaps, is_pow2, ilog2,
// isqrt, gcd, lcm, mul_hi and mul_wide -- at the edges of their domains.
// Valid input trips none of GS_DEBUG's checks inside std.
import std;

fn rotations() {
    let x: u32 = 0x80000001;
    let y: u64 = 0x8000000000000001;
    print(rotl(x, 1), " ", rotr(x, 1), " ", rotl(x, 0), " ", rotr(x, 0));   // 3 3221225472 2147483649 2147483649
    print(rotl(y, 1), " ", rotr(y, 4), " ", rotl(y, 0));                     // 3 1729382256910270464 9223372036854775809
    // Counts are taken modulo the width; a negative count turns the other way.
    print(rotl(x, 32) == x, " ", rotl(x, 33) == rotl(x, 1), " ", rotl(x, -1) == rotr(x, 1));  // true true true
    print(rotr(y, 64) == y, " ", rotr(y, 130) == rotr(y, 2), " ", rotr(y, -3) == rotl(y, 3)); // true true true
    let mn = -9223372036854775807 - 1;
    print(rotl(x, mn) == x, " ", rotr(y, mn) == y);                          // true true
    let small: u32 = 0x12345678;
    print(rotl(small, 8), " ", rotr(small, 8));                              // 878082066 2014458966
    // The count is any integer type: a u32 one, as hashes keep them, or an i8.
    let s32: u32 = 8;
    let s8: i8 = -8;
    print(rotl(small, s32), " ", rotr(small, s8), " ", rotl(y, s32 + 57));     // 878082066 878082066 3
    // A u8 matches both widths equally, so it names the one to rotate at.
    let b: u8 = 0x81;
    print(rotl(b as u32, 4));                                                // 2064
}

fn swaps() {
    let a: u16 = 0x1234;
    let b: u32 = 0x12345678;
    let c: u64 = 0x0102030405060708;
    print(byte_swap(a), " ", byte_swap(b), " ", byte_swap(c));  // 13330 2018915346 578437695752307201
    let z16: u16 = 0;
    let m16: u16 = 0xFFFF;
    let m32: u32 = 0xFF;
    let m64: u64 = 0xFF;
    print(byte_swap(z16), " ", byte_swap(m16), " ", byte_swap(m32), " ", byte_swap(m64));  // 0 65535 4278190080 18374686479671623680
    print(byte_swap(byte_swap(c)) == c, " ", byte_swap(byte_swap(b)) == b);              // true true
}

fn powers() {
    print(is_pow2(0), " ", is_pow2(1), " ", is_pow2(2), " ", is_pow2(6), " ", is_pow2(64));  // false true true false true
    let mn = -9223372036854775807 - 1;
    let top: u64 = 0x8000000000000000;
    let all: u64 = 0xFFFFFFFFFFFFFFFF;
    print(is_pow2(-4), " ", is_pow2(mn), " ", is_pow2(top), " ", is_pow2(all));             // false false true false
    let b: u8 = 128;
    print(is_pow2(b), " ", ilog2(b));                                                     // true 7
    print(ilog2(0), " ", ilog2(-5), " ", ilog2(mn), " ", ilog2(1), " ", ilog2(2), " ", ilog2(3));  // -1 -1 -1 0 1 1
    print(ilog2(1023), " ", ilog2(1024), " ", ilog2(top), " ", ilog2(all));               // 9 10 63 63
    print(ilog2(9223372036854775807));                                                    // 62
}

fn roots() {
    print(isqrt(0), " ", isqrt(1), " ", isqrt(2), " ", isqrt(3), " ", isqrt(4), " ", isqrt(8), " ", isqrt(9));  // 0 1 1 1 2 2 3
    print(isqrt(99), " ", isqrt(100), " ", isqrt(1000000));                              // 9 10 1000
    print(isqrt(9223372036854775807));                                                   // 3037000499
    let all: u64 = 0xFFFFFFFFFFFFFFFF;
    let sq: u64 = 0xFFFFFFFE00000001;   // (2^32 - 1)^2
    print(isqrt(all), " ", isqrt(sq), " ", isqrt(sq - 1));                              // 4294967295 4294967295 4294967294
    let b: u8 = 255;
    let h: i16 = 32767;
    print(isqrt(b), " ", isqrt(h));                                                      // 15 181
    // Every root up to 2^20 squares at or below its argument, and the next
    // one above it.
    var ok = true;
    for i in 1 << 20 {
        let r = isqrt(i);
        if r * r > i || (r + 1) * (r + 1) <= i { ok = false; }
    }
    print(ok);                                                                           // true
}

fn divisors() {
    print(gcd(0, 0), " ", gcd(0, 5), " ", gcd(5, 0), " ", gcd(12, 18), " ", gcd(17, 5));  // 0 5 5 6 1
    print(gcd(-12, 18), " ", gcd(12, -18), " ", gcd(-12, -18), " ", gcd(-7, 0));        // 6 6 6 7
    let mn = -9223372036854775807 - 1;
    print(gcd(mn, 6), " ", gcd(mn, -1), " ", gcd(mn, 1 << 62));                         // 2 1 4611686018427387904
    let top: u64 = 0x8000000000000000;
    let all: u64 = 0xFFFFFFFFFFFFFFFF;
    let three: u64 = 3;
    print(gcd(top, 0), " ", gcd(all, three), " ", gcd(all, all));                       // 9223372036854775808 3 18446744073709551615
    let p: i8 = -128;
    let q: i8 = 96;
    print(gcd(p, q));                                                                   // 32
    print(lcm(0, 0), " ", lcm(0, 7), " ", lcm(4, 6), " ", lcm(-4, 6), " ", lcm(-4, -6), " ", lcm(5, 1));  // 0 0 12 12 12 5
    print(lcm(1 << 31, 1 << 31), " ", lcm(4294967296, 2147483647));                    // 2147483648 9223372032559808512
    let one: u64 = 1;
    let hi: u64 = 4294967296;
    print(lcm(top, one), " ", lcm(all, one), " ", lcm(hi, hi - 1));                    // 9223372036854775808 18446744073709551615 18446744069414584320
    let r: u8 = 15;
    let s: u8 = 17;
    let t: i8 = -127;
    print(lcm(r, s), " ", lcm(t, 1), " ", lcm(mn + 1, -1));                       // 255 127 9223372036854775807
}

// mul_hi and mul_wide at the edges, and mul_hi against four 32-bit products
// over a pseudo-random sequence (the runtime's instruction or its fallback).
fn products() {
    let m: u64 = 0xFFFFFFFFFFFFFFFF;
    let pairs: u64[14] = [0, 0, 1, 1, m, m, 0x8000000000000000, 2, 0x0123456789ABCDEF,
                          0xFEDCBA9876543210, 0x100000000, 0x100000000, m, 1];
    for i in 7 {
        let hi, lo = mul_wide(pairs[2 * i], pairs[2 * i + 1]);
        print(hi, " ", lo);
    }
    var x: u64 = 0x9E3779B97F4A7C15;
    var bad = 0;
    for i in 10000 {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        let y = x * 0xD1B54A32D192ED03 + i as! u64;
        let a0 = x & 0xFFFFFFFF;
        let a1 = x >> 32;
        let b0 = y & 0xFFFFFFFF;
        let b1 = y >> 32;
        let mid = ((a0 * b0) >> 32) + ((a0 * b1) & 0xFFFFFFFF) + ((a1 * b0) & 0xFFFFFFFF);
        let hi = a1 * b1 + ((a0 * b1) >> 32) + ((a1 * b0) >> 32) + (mid >> 32);
        if mul_hi(x, y) != hi { bad++; }
    }
    print("mul_hi mismatches: ", bad);
}

// add_carry and sub_borrow at the edges, and against 32-bit halves over a
// pseudo-random sequence.
fn carries() {
    let m: u64 = 0xFFFFFFFFFFFFFFFF;
    let s0, c0 = add_carry(m, 0, 1);
    let s1, c1 = add_carry(m, m, 1);
    let s2, c2 = add_carry(1, 2, 0);
    let d0, b0 = sub_borrow(0, 0, 1);
    let d1, b1 = sub_borrow(0, m, 1);
    let d2, b2 = sub_borrow(5, 3, 1);
    print(s0, " ", c0, " ", s1, " ", c1, " ", s2, " ", c2);                    // 0 1 18446744073709551615 1 3 0
    print(d0, " ", b0, " ", d1, " ", b1, " ", d2, " ", b2);                    // 18446744073709551615 1 0 1 1 0
    var x: u64 = 0x9E3779B97F4A7C15;
    var bad = 0;
    for i in 10000 {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        let y = x * 0xD1B54A32D192ED03;
        let c = (x >> 17) & 1;
        let lo = (x & 0xFFFFFFFF) + (y & 0xFFFFFFFF) + c;
        let hi = (x >> 32) + (y >> 32) + (lo >> 32);
        let s, carry = add_carry(x, y, c);
        if s != (hi << 32 | (lo & 0xFFFFFFFF)) || carry != hi >> 32 { bad++; }
        // a - b - c borrows when b + c > a.
        let d, borrow = sub_borrow(x, y, c);
        let want: u64 = if y > x || (y == x && c == 1) { 1 } else { 0 };
        if d + y + c != x || borrow != want { bad++; }
    }
    print("add_carry and sub_borrow mismatches: ", bad);
}

fn main() {
    rotations();
    swaps();
    powers();
    roots();
    divisors();
    products();
    carries();
}
