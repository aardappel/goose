// stdlib/math.goose: libm through extern fns, both widths, and the rest.
import math;

fn main() {
    let x: f32 = 2.25;
    print(sqrt(16.0), " ", sqrt(x));                                              // 4 1.5
    print(floor(2.7), " ", ceil(2.1), " ", round(2.5), " ", trunc(-2.7));         // 2 3 3 -2
    print(pow(2.0, 10.0), " ", exp(0.0), " ", log(1.0), " ", log2(8.0), " ", log10(1000.0));   // 1024 1 0 3 3
    print(sin(0.0), " ", cos(0.0), " ", atan2(1.0, 1.0) == PI / 4.0);              // 0 1 true
    print(radians(180.0) == PI, " ", degrees(PI) == 180.0, " ", TAU == 2.0 * PI);   // true true true
    let z = 0.0;
    let nan = z / z;
    print(is_nan(nan), " ", is_inf(1.0 / z), " ", is_nan(1.0), " ", is_inf(1.0));   // true true false false
    let y: f32 = 0.5;
    print(floor(y), " ", pow(x, y));                                              // 0 1.5
}
