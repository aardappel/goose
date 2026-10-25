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
}
