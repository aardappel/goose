// `simd fn` (spec §7.12): clang on x86-64 builds each of these once per
// instruction-set level, and the program runs the highest its CPU has;
// elsewhere they are ordinary functions. Every version must print what the
// baseline prints, which the runner checks by building this file with
// clang at each level (GS_SIMD=0, 1, 2) beside the native and TinyCC runs.
// The loops are shapes clang vectorizes, every signature shape a version
// forwards is here (slices, fat references, a nonfixed result and its
// element-run twin, several results, captures, generics, recursion, an
// export), and the float kernel would print other digits if any version
// fused its multiply and add.

import std;

// Bytes shifted within their alphabet: a select per element, vectorized.
simd fn caesar(dst: u8[:], src: u8[:], k: u8) {
    assert(dst.len == src.len);
    for c, i in src {
        var d = c;
        if c - 'a' < 26 { d = 'a' + (c - 'a' + k) % 26; }
        if c - 'A' < 26 { d = 'A' + (c - 'A' + k) % 26; }
        dst[i] = d;
    }
}

// An unsigned reduction, which clang may reassociate.
simd fn checksum(xs: u8[:]) -> u32 {
    var h: u32 = 0;
    for x in xs { h += x as u32 * 31 + 7; }
    return h;
}

// c + a * b per element through a growable result: rounded twice in every
// version, never fused into one rounding. The product is the right operand
// so that the C has both operations in one expression, which is what clang
// contracts where FMA is available (the AVX-512 version) unless told not to.
simd fn muladd(out: f64[>..]&, a: f64[:], b: f64[:], c: f64[:]) {
    assert(a.len == b.len && b.len == c.len);
    let start = out.len;
    out.resize(start + a.len, 0.0);
    let dst = out[start..];
    assert(dst.len == a.len);
    for i in a.len {
        let x = a[i];
        let y = b[i];
        let z = c[i];
        dst[i] = z + x * y;
    }
}

simd fn squares(n: i64) -> i64[] {
    var r: i64[>..] = [];
    for i in n { r.push(i * i); }
    return copy(r);
}

simd fn minmax(xs: i64[:]) -> i64, i64 {
    var lo = xs[0];
    var hi = xs[0];
    for x in xs {
        lo = min(lo, x);
        hi = max(hi, x);
    }
    return lo, hi;
}

simd fn total<T>(xs: T[:]) -> T {
    var s: T = 0;
    for x in xs { s += x; }
    return s;
}

simd recursive fn fib(n: i64) -> i64 {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);
}

export "simd_sum3" simd fn sum3(a: i32, b: i32, c: i32) -> i32 { return a + b + c; }

fn main() {
    var text: u8[>..] = [];
    for i in 256 { text.push(i as! u8); }
    text.append("The quick brown fox jumps over the lazy dog");
    var shifted: u8[>..] = [];
    shifted.resize(text.len, 0);
    caesar(shifted, text, 13);
    print(shifted[256..]);
    print(checksum(text), " ", checksum(shifted));

    // 1 + 2^-30 times 1 - 2^-30 is 1 - 2^-60, which rounds to 1; adding -1
    // then gives 0, where a fused multiply-add gives -2^-60.
    let e = 1.0 / 1073741824.0;
    var a: f64[>..] = [];
    var b: f64[>..] = [];
    var c: f64[>..] = [];
    for i in 40 {
        a.push(1.0 + e * (i % 3));
        b.push(1.0 - e * (i % 3));
        c.push(-1.0);
    }
    var r: f64[>..] = [];
    muladd(r, a, b, c);
    var zeros = 0;
    for x in r { if x == 0.0 { zeros++; } }
    print(zeros, " of ", r.len, " ", r[1], " ", r[2]);

    var sq: i64[>..] = [100];
    sq.append(squares(6));
    print(sq);
    let lo, hi = minmax(sq);
    print(lo, " ", hi);
    let small: i32[5] = [1, 2, 3, 4, 5];
    let wide: u16[3] = [60000, 5000, 536];
    print(total(small), " ", total(wide));

    var bias = 3;
    simd fn biased(x: i64) -> i64 { return x * 2 + bias; }
    bias = 10;
    print(biased(4), " ", fib(20), " ", sum3(1, 2, 3));
}
