// math's mul_add (spec §7.12): x * y + z rounded once where the code is
// built for FMA, twice where it is not, and so the one result a simd
// function's versions may give differently from its baseline. A plain
// function has the target's rounding; a simd function's AVX2 and AVX-512
// versions round once. (1 + e)^2 - (1 + 2e) tells them apart: e^2 rounded
// once, 0 with the square rounded first (e = 2^-30).
//
// In a simd function the call fuses where Goose emits it in the function's
// own body: written there, in an inline helper Goose inlines, or through a
// function value, which is resolved at compile time. In a plain helper the
// call keeps the helper's rounding unless Goose inlines it, which -O0
// never does.
//
// Every build prints that each result is one or the other. Given `modes`,
// the program also says which rounding each used, which the runner checks
// against the build: clang at each simd level, on this machine's CPU, at
// Goose -O2 and -O0.

import std;
import os;
import math;

fn residual(x: f64) -> f64 { mul_add(x, x, -(1.0 + 2.0 * (x - 1.0))) }
inline fn residual_inline(x: f64) -> f64 { mul_add(x, x, -(1.0 + 2.0 * (x - 1.0))) }

// The same per element, in loops clang vectorizes.
simd fn direct(out: f64[:], xs: f64[:]) {
    assert(out.len == xs.len);
    for x, i in xs { out[i] = mul_add(x, x, -(1.0 + 2.0 * (x - 1.0))); }
}

simd fn via_inline(out: f64[:], xs: f64[:]) {
    assert(out.len == xs.len);
    for x, i in xs { out[i] = residual_inline(x); }
}

simd fn via_helper(out: f64[:], xs: f64[:]) {
    assert(out.len == xs.len);
    for x, i in xs { out[i] = residual(x); }
}

simd fn via_value<F>(out: f64[:], xs: f64[:]) {
    assert(out.len == xs.len);
    for x, i in xs { out[i] = F(x, x, -(1.0 + 2.0 * (x - 1.0))); }
}

fn mode(r: f64) -> u8[:] { if r == 0.0 { "separate" } else { "fused" } }

fn main() {
    let e = 1.0 / 1073741824.0;
    var xs: f64[>..] = [];
    for i in 9 { xs.push(1.0 + e); }
    var ok = true;
    var modes: u8[>..] = [];
    fn report(r: f64) {
        ok = ok && (r == 0.0 || r == e * e);
        if modes.len > 0 { modes.push(' '); }
        modes.append(mode(r));
    }
    // Each of the simd functions' results is the same in every element.
    fn each(out: f64[:]) {
        for r in out { ok = ok && r == out[0]; }
        report(out[0]);
    }
    report(residual(xs[0]));
    var out: f64[>..] = [];
    out.resize(xs.len, 7.0);
    direct(out, xs);
    each(out);
    via_inline(out, xs);
    each(out);
    via_helper(out, xs);
    each(out);
    via_value(out, xs, mul_add);
    each(out);
    print(ok);   // true
    let argv = args();
    if argv.len > 1 && argv[1] == "modes" { print(modes); }
}
