// runtime-debug: float to integer with `as!` (§6.3) truncates toward zero
// and wraps modulo 2^64 into the target's width, NaN giving 0, in every
// build: values inside the i64 range up to its ends, and values past them
// to beyond 2^64, into each width.
import std;

fn main() {
    // Read from arrays, so no value is known to the optimizer.
    var ds: f64[>..] = [0.0, -0.0, 0.5, -0.5, 0.9999999999999999, -0.9999999999999999, 2.5, -2.5,
                        255.9, 256.0, -129.5, 65535.75, 4294967296.5, 4503599627370497.0,
                        9223372036854774784.0, -9223372036854774784.0, -9223372036854775808.0,
                        9223372036854775808.0, -9223372036854777856.0, 12345678901234567890.0,
                        18446744073709549568.0, 18446744073709551616.0, -18446744073709551616.0,
                        18446744073709555712.0, -18446744073709555712.0, 30000000000000000000.0,
                        -30000000000000000000.0, 1e300, -1e300];
    for d in ds {
        print(d, " ", d as! i64, " ", d as! u64, " ", d as! i32, " ", d as! u32, " ", d as! i16, " ",
              d as! u8);
    }
    let nan = ds[0] / ds[0];
    print(nan as! i64, " ", nan as! u64, " ", nan as! i8);
    var fs: f32[>..] = [-1.5, 3e9, -3e9, 1e19, 1e20];
    for f in fs { print(f, " ", f as! i64, " ", f as! u64, " ", f as! i32); }
}
