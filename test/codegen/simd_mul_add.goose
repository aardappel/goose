// math's mul_add (spec §7.12): x * y + z rounded once where the code is
// built for FMA, twice where it is not, and so the one result a simd
// function's versions may give differently from its baseline. A plain
// function has the target's rounding; a simd function's AVX2 and AVX-512
// versions round once. (1 + e)^2 - (1 + 2e) tells them apart: e^2 rounded
// once, 0 with the square rounded first (e = 2^-30).
//
// Every build prints that each result is one or the other. Given `modes`,
// the program also says which rounding each used, which the runner checks
// against the build: clang at each simd level, on this machine's CPU.

import std;
import os;
import math;

fn residual(x: f64) -> f64 { mul_add(x, x, -(1.0 + 2.0 * (x - 1.0))) }

// The same per element, in a loop clang vectorizes.
simd fn residuals(out: f64[:], xs: f64[:]) {
    assert(out.len == xs.len);
    for x, i in xs { out[i] = mul_add(x, x, -(1.0 + 2.0 * (x - 1.0))); }
}

fn mode(r: f64) -> u8[:] { if r == 0.0 { "separate" } else { "fused" } }

fn main() {
    let e = 1.0 / 1073741824.0;
    var xs: f64[>..] = [];
    for i in 9 { xs.push(1.0 + e); }
    var out: f64[>..] = [];
    out.resize(xs.len, 7.0);
    residuals(out, xs);
    let p = residual(xs[0]);
    let s = out[0];
    var same = true;
    for r in out { same = same && r == s; }
    print(p == 0.0 || p == e * e, " ", s == 0.0 || s == e * e, " ", same);   // true true true
    let argv = args();
    if argv.len > 1 && argv[1] == "modes" { print(mode(p), " ", mode(s)); }
}
