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
}
