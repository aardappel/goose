// LangArena's shared helpers: the benchmark-suite random number generator,
// the djb2 checksums, and the fixed-precision float text the checksums of
// float results are taken over.

import std;
import math;

let IM = 139968;
let IA = 3877;
let IC = 29573;
let INIT = 42;

var rng_last: i32 = 42;

fn reset() { rng_last = INIT; }

fn next_int(max: i64) -> i32 {
    rng_last = (rng_last * IA + IC) % IM;
    return (rng_last / IM as f64 * max) as! i32;
}

fn next_float(max: f64) -> f64 {
    rng_last = (rng_last * IA + IC) % IM;
    return max * rng_last / IM;
}

fn checksum_str(v: u8[:]) -> u32 {
    var hash: u32 = 5381;
    for b in v { hash = (hash << 5) + hash + b; }
    return hash;
}

fn checksum_f64(v: f64) -> u32 {
    var s: u8[>..] = [];
    format_fixed(s, v, 7);
    return checksum_str(s);
}

// --- exact fixed-point float text ---------------------------------------------

// The C library pair that makes the conversion below exact: the rounding
// error of a product, and a double's mantissa and exponent.
extern fn fma(x: f64, y: f64, z: f64) -> f64;
extern fn frexp(x: f64, e: i32&) -> f64;

// `v` with `decimals` digits after the point, correctly rounded from its exact
// binary value with ties to even: what Rust's `{:.N}`, C's `%.Nf` and C++'s
// `std::fixed` print, digit for digit. `decimals` is at most 15.
fn format_fixed(out: u8[>..]&, v: f64, decimals: i64) {
    if v != v { out.append("NaN"); return; }
    if v - v != 0.0 { out.append(if v > 0.0 { "inf" } else { "-inf" }); return; }
    var x = v;
    if x < 0.0 { out.push('-'); x = -x; }
    var scale = 1.0;
    var iscale: u64 = 1;
    for i in decimals { scale *= 10.0; iscale *= 10; }
    let p = x * scale;
    var whole: u64 = 0;
    var frac: u64 = 0;
    if p < 4503599627370496.0 {
        // Below 2^52, x * scale is p plus the exactly representable error
        // fma recovers, and both halves of the fraction test are exact.
        let err = fma(x, scale, -p);
        let n = floor(p);
        let t = (p - n) - 0.5;
        var r = n as! u64;
        if t > 0.0 || (t == 0.0 && err > 0.0) || (t == 0.0 && err == 0.0 && r % 2 == 1) { r += 1; }
        whole = r / iscale;
        frac = r % iscale;
    } else {
        // At or above 2^52 / scale, x = m / 2^k with k small enough that the
        // fraction's digits come out of plain integer arithmetic.
        var e: i32 = 0;
        let f = frexp(x, e);
        let m = (f * 9007199254740992.0) as! u64;     // 53 bits
        let k = 53 - e as i64;
        if k <= 0 {
            whole = m << (-k);
        } else {
            whole = m >> k;
            let mask = ((1 as u64) << k) - 1;
            var rest = m & mask;
            for i in decimals {
                rest *= 10;
                frac = frac * 10 + (rest >> k);
                rest &= mask;
            }
            let half = (1 as u64) << (k - 1);
            if rest > half || (rest == half && frac % 2 == 1) {
                frac += 1;
                if frac == iscale { frac = 0; whole += 1; }
            }
        }
    }
    format_uint(out, whole, 10, 0, ' ');
    if decimals > 0 {
        out.push('.');
        format_uint(out, frac, 10, decimals, '0');
    }
}
