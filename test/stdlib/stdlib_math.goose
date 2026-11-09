// stdlib/math.goose: libm through extern fns, both widths, and the rest.
// Transcendental results allow rounding differences between native libm and JIT.
import math;
import std;

fn main() {
    let x: f32 = 2.25;
    print(sqrt(16.0), " ", sqrt(x));                                              // 4.0 1.5
    print(floor(2.7), " ", ceil(2.1), " ", round(2.5), " ", trunc(-2.7));         // 2.0 3.0 3.0 -2.0
    print(pow(2.0, 10.0), " ", exp(0.0), " ", log(1.0), " ", abs(log2(8.0) - 3.0) < 1e-12, " ", log10(1000.0));   // 1024.0 1.0 0.0 true 3.0
    print(sin(0.0), " ", cos(0.0), " ", abs(atan2(1.0, 1.0) - PI / 4.0) < 1e-12);  // 0.0 1.0 true
    print(radians(180.0) == PI, " ", degrees(PI) == 180.0, " ", TAU == 2.0 * PI);   // true true true
    let z = 0.0;
    let nan = z / z;
    print(is_nan(nan), " ", is_inf(1.0 / z), " ", is_nan(1.0), " ", is_inf(1.0));   // true true false false
    print(is_finite(1.0), " ", is_finite(1.0 / z), " ", is_finite(nan), " ", is_finite(-1e308));   // true false false true
    // The f32 forms stay f32, and degrees multiplies by 180 / pi rounded to
    // f32 once (57.29578, where 180 / PI in f32 is 57.295776).
    let half_turn: f32 = 180.0;
    let one_rad: f32 = 1.0;
    print(radians(half_turn), " ", degrees(one_rad), " ", degrees(radians(half_turn)));   // 3.1415927 57.29578 180.0
    let zf: f32 = 0.0;
    print(is_nan(zf / zf), " ", is_inf(-1.0 / zf), " ", is_finite(zf), " ", is_finite(zf / zf));   // true true true false
    let y: f32 = 0.5;
    print(floor(y), " ", pow(x, y));                                              // 0.0 1.5
    // sqrt is the runtime's, with no errno: the correctly rounded root, -0.0
    // for -0.0 and NaN below zero, at both widths. Read from arrays, so no
    // value is known to the optimizer.
    var sq: f64[>..] = [2.0, -0.0, -1.0, 1e308];
    var sqf: f32[>..] = [2.0, -4.0];
    print(sqrt(sq[0]), " ", sqrt(sq[1]), " ", is_nan(sqrt(sq[2])), " ", sqrt(sq[3]), " ", sqrt(sqf[0]),
          " ", is_nan(sqrt(sqf[1])));   // 1.4142135623730951 -0.0 true 1e+154 1.4142135 true
    // mul_add rounds x * y + z once where the code is built for FMA and twice
    // where it is not (§7.12), so only what both give is printed. Exact
    // products agree; (1 + e)^2 - (1 + 2e) is e^2 rounded once, and 0 when
    // the square is rounded first (e = 2^-30, and 2^-12 for f32).
    var ma: f64[>..] = [2.0, 3.0, 4.0, 1.0 + 1.0 / 1073741824.0];
    var maf: f32[>..] = [2.0, 3.0, 4.0, 1.0 + 1.0 / 4096.0];
    let d = mul_add(ma[3], ma[3], -(1.0 + 2.0 / 1073741824.0));
    let one_e: f32 = 1.0;
    let df = mul_add(maf[3], maf[3], -(one_e + 2.0 / 4096.0));
    print(mul_add(ma[0], ma[1], ma[2]), " ", d == 0.0 || d == 1.0 / (1073741824.0 * 1073741824.0), " ",
          mul_add(maf[0], maf[1], maf[2]), " ", df == 0.0 || df == 1.0 / (4096.0 * 4096.0));   // 10.0 true 10.0 true
    // An intermediate overflow: 1e308 * 2 overflows unless it is kept for
    // the add, so the result is 1e308 fused and inf unfused (3e38 and inf
    // for f32), and nothing else.
    var big: f64[>..] = [1.0e308, 2.0];
    var bigf: f32[>..] = [3.0e38, 2.0];
    let o = mul_add(big[0], big[1], -big[0]);
    let of = mul_add(bigf[0], bigf[1], -bigf[0]);
    print(o == 1.0e308 || is_inf(o), " ", of == bigf[0] || is_inf(of));   // true true
    // hypot cbrt exp2 expm1 log1p sinh cosh tanh are libm's, so only the
    // values C's Annex F fixes are printed exactly, and the rest within a
    // tolerance. Read from arrays, so no value is known to the optimizer.
    var hx: f64[>..] = [3.0, 4.0, 27.0, -8.0, 10.0, 1e-10, 1.0, 1e300];
    print(abs(hypot(hx[0], hx[1]) - 5.0) < 1e-12, " ", abs(cbrt(hx[2]) - 3.0) < 1e-12, " ",
          abs(cbrt(hx[3]) + 2.0) < 1e-12, " ", abs(exp2(hx[4]) - 1024.0) < 1e-9);   // true true true true
    print(hypot(hx[7], hx[7]) > 1e300, " ", is_finite(hypot(hx[7], hx[7])));   // true true
    // expm1 and log1p keep a tiny x's digits, x + x^2/2 and x - x^2/2,
    // which exp(x) - 1 and log(1 + x) lose.
    let tiny = hx[5];
    print(abs(expm1(tiny) / (tiny * (1.0 + tiny / 2.0)) - 1.0) < 1e-14, " ",
          abs(log1p(tiny) / (tiny * (1.0 - tiny / 2.0)) - 1.0) < 1e-14, " ",
          abs((exp(tiny) - 1.0) / tiny - 1.0) > 1e-9);   // true true true
    print(abs(sinh(hx[6]) - (E - 1.0 / E) / 2.0) < 1e-12, " ", abs(cosh(hx[6]) - (E + 1.0 / E) / 2.0) < 1e-12, " ",
          abs(tanh(hx[6]) - sinh(hx[6]) / cosh(hx[6])) < 1e-12, " ", abs(exp(1.0) - E) < 1e-15);   // true true true true
    // Annex F's special cases: -0.0 kept, and the limits at the infinities.
    var sp: f64[>..] = [0.0, -0.0, -1.0];
    let inf = INF;
    print(expm1(sp[1]), " ", log1p(sp[1]), " ", sinh(sp[1]), " ", tanh(sp[1]), " ", cbrt(sp[1]), " ", cosh(sp[0]),
          " ", exp2(sp[0]));   // -0.0 -0.0 -0.0 -0.0 -0.0 1.0 1.0
    print(tanh(inf), " ", tanh(-inf), " ", cosh(-inf), " ", sinh(-inf), " ", expm1(-inf), " ", exp2(-inf), " ",
          log1p(sp[2]), " ", cbrt(-inf), " ", hypot(-inf, NAN));   // 1.0 -1.0 inf -inf -1.0 0.0 -inf -inf inf
    let hf: f32 = 3.0;
    let hf4: f32 = 4.0;
    let tf: f32 = 0.5;
    print(hypot(hf, hf4), " ", abs(cbrt(hf * 9.0) - 3.0) < 1e-5, " ", abs(exp2(hf) - 8.0) < 1e-5, " ",
          abs(tanh(tf) - sinh(tf) / cosh(tf)) < 1e-6, " ", abs(expm1(tf) - (exp(tf) - 1.0)) < 1e-6, " ",
          abs(log1p(tf) - log(1.5 as f32)) < 1e-6);   // 5.0 true true true true true
    // copysign reads the sign bit: -0.0's, and an infinity's.
    var cs: f64[>..] = [3.0, -0.0, 0.0, -2.0];
    var csf: f32[>..] = [3.0, -1.0];
    print(copysign(cs[0], cs[1]), " ", copysign(cs[2], cs[3]), " ", copysign(cs[3], cs[2]), " ",
          copysign(cs[0], -inf), " ", copysign(inf, cs[3]), " ", copysign(csf[0], csf[1]));   // -3.0 -0.0 2.0 -3.0 -inf -3.0
    print(E, " ", INF, " ", -INF, " ", is_nan(NAN), " ", is_inf(INF), " ", INF > 1e308);   // 2.718281828459045 inf -inf true true true
    let inf32: f32 = INF;
    print(is_inf(inf32), " ", is_nan(NAN as f32));   // true true
    // fract is x - floor(x), as GLSL: never negative.
    var fr: f64[>..] = [2.75, -0.25, -0.0, -3.0, 1e300];
    let frf: f32 = -1.75;
    print(fract(fr[0]), " ", fract(fr[1]), " ", fract(fr[2]), " ", fract(fr[3]), " ", fract(fr[4]), " ", fract(frf),
          " ", is_nan(fract(inf)), " ", is_nan(fract(NAN)));   // 0.75 0.75 0.0 0.0 0.0 0.25 true true
    // inverse_lerp undoes lerp and is not clamped.
    let ia: f32 = 2.0;
    let ib: f32 = 6.0;
    let iv: f32 = 3.0;
    print(inverse_lerp(10.0, 20.0, 15.0), " ", inverse_lerp(10.0, 20.0, 25.0), " ", inverse_lerp(10.0, 20.0, 5.0),
          " ", inverse_lerp(20.0, 10.0, 12.5), " ", inverse_lerp(ia, ib, iv), " ",
          inverse_lerp(10.0, 20.0, lerp(10.0, 20.0, 0.375)), " ", is_nan(inverse_lerp(1.0, 1.0, 1.0)));   // 0.5 1.5 -0.5 0.75 0.25 0.375 true
    // smoothstep as GLSL; reversed edges run it backwards, and equal edges
    // step, with NaN at the edge.
    let s0: f32 = 0.0;
    let s1: f32 = 4.0;
    let sx: f32 = 1.0;
    print(smoothstep(0.0, 1.0, -1.0), " ", smoothstep(0.0, 1.0, 0.0), " ", smoothstep(0.0, 1.0, 0.25), " ",
          smoothstep(0.0, 1.0, 0.5), " ", smoothstep(0.0, 1.0, 1.0), " ", smoothstep(0.0, 1.0, 2.0), " ",
          smoothstep(1.0, 0.0, 0.25), " ", smoothstep(s0, s1, sx));   // 0.0 0.0 0.15625 0.5 1.0 1.0 0.84375 0.15625
    print(smoothstep(1.0, 1.0, 0.5), " ", smoothstep(1.0, 1.0, 1.5), " ", is_nan(smoothstep(1.0, 1.0, 1.0)), " ",
          is_nan(smoothstep(0.0, 1.0, NAN)));   // 0.0 1.0 true true
    // wrap_angle brings an angle into [-PI, PI): PI goes to -PI, and -0.0
    // stays.
    var wa: f64[>..] = [0.5, PI, -PI, 3.0 * PI / 2.0, -0.0, 7.0 * TAU + 1.0, -5.0 * TAU - 1.0,
                        1e10, 1e18, 1.1319666049798702e17];
    print(wrap_angle(wa[0]), " ", wrap_angle(wa[1]) == -PI, " ", wrap_angle(wa[2]) == -PI, " ",
          abs(wrap_angle(wa[3]) + PI / 2.0) < 1e-12, " ", wrap_angle(wa[4]), " ", abs(wrap_angle(wa[5]) - 1.0) < 1e-12,
          " ", abs(wrap_angle(wa[6]) + 1.0) < 1e-12);   // 0.5 true true true -0.0 true true
    let w10 = wrap_angle(wa[7]);
    let w18 = wrap_angle(wa[8]);
    let w17 = wrap_angle(wa[9]);
    print(w10 >= -PI && w10 < PI, " ", w18 >= -PI && w18 < PI, " ", w17 >= -PI && w17 < PI,
          " ", is_nan(wrap_angle(inf)), " ", is_nan(wrap_angle(NAN)));   // true true true true true
    var waf: f32[>..] = [3.1415927, -3.1415927, 7.0, -7.0, 1e9, 210858020.0];
    let pif: f32 = PI;
    print(wrap_angle(waf[0]) == -pif, " ", wrap_angle(waf[1]) == -pif, " ", abs(wrap_angle(waf[2]) - (7.0 - pif * 2.0)) < 1e-5,
          " ", abs(wrap_angle(waf[3]) + (7.0 - pif * 2.0)) < 1e-5);   // true true true true
    let wf9 = wrap_angle(waf[4]);
    let wf8 = wrap_angle(waf[5]);
    print(wf9 >= -pif && wf9 < pif, " ", wf8 >= -pif && wf8 < pif);   // true true
    // Every wrapped value lies in [-PI, PI), at both widths.
    var ok = true;
    for i in 0..2000 {
        let a = (i - 1000) as f64 * 0.0377;
        let w = wrap_angle(a);
        let wf = wrap_angle(a as f32);
        ok = ok && w >= -PI && w < PI && wf >= -pif && wf < pif && abs(fract(a) - (a - floor(a))) == 0.0;
    }
    print(ok);   // true
}
