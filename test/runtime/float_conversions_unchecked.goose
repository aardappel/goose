// runtime-debug: a conversion to a float never aborts, in a debug build
// either (§6.3), and rounds to the nearest value of its type.

// A conversion to f32 rounds to the nearest f32, from an f64 or from any
// integer, and beyond f32's range it is an infinity.
fn f32_conversions() {
    // Read from arrays, so no value is known to the optimizer.
    var doubles: f64[>..] = [0.1, 1e300, -1e300, 16777217.0];
    var ints: i64[>..] = [16777217, 123456789, 9223372036854775807, 1152921573326323713];
    var halves: i32[>..] = [2147483647];
    var words: u32[>..] = [4294967295];
    var wides: u64[>..] = [18446744073709551615, 9223372586610589697, 9223373686122217471,
                           18446743523953737727];

    let a = doubles[0] as f32;
    print(a);                               // 0.1
    let up = doubles[1] as f32;
    print(up > 3.4e38, " ", up == up * 2.0);            // true true
    let down = doubles[2] as f32;
    print(down < -3.4e38, " ", down == down * 2.0);     // true true
    print(doubles[3] as f32);               // 16777216.0
    print(ints[0] as f32);                  // 16777216.0
    print(ints[1] as f32);                  // 123456790.0
    print(ints[2] as f32);                  // 9.223372e+18
    // 2^60 + 2^36 + 1 rounds up to 2^60 + 2^37. Through f64 it would first
    // round to the tie 2^60 + 2^36, and from there down to 2^60.
    print(ints[3] as f32);                  // 1.1529216e+18
    print(halves[0] as f32);                // 2147483600.0
    print(words[0] as f32);                 // 4294967300.0
    print(wides[0] as f32);                 // 1.8446744e+19
    // Past 2^63, where a u64 no longer fits an i64: 2^63 + 2^39 + 1 rounds
    // up to 2^63 + 2^40 and 2^63 + 2^40 + 2^39 - 1 down to it, and
    // 2^64 - 2^39 - 1 rounds down to 2^64 - 2^40. Through f64 each would
    // first round to a tie, and from there to the other neighbor.
    print(wides[1] as f32, " ", wides[2] as f32);       // 9.223373e+18 9.223373e+18
    print(wides[3] as f32);                 // 1.8446743e+19
    // Into a wider slot, the value is still the rounded one.
    let d: f64 = doubles[0] as f32;
    print(d);                               // 0.10000000149011612
    print((doubles[0] as f32) == (doubles[0] as! f32));  // true

    // Constants, which the optimizer folds.
    print(0.1 as f32);                      // 0.1
    let cup = 1e300 as f32;
    print(cup > 3.4e38, " ", cup == cup * 2.0);         // true true
    print(16777217 as f32);                 // 16777216.0
    print(9223372036854775807 as f32);      // 9.223372e+18
    print(1152921573326323713 as f32);      // 1.1529216e+18
    print(18446744073709551615 as f32);     // 1.8446744e+19
    print(9223373686122217471 as f32);      // 9.223373e+18
    let w: u64 = 18446743523953737727;
    let wf: f32 = w;
    print(wf);                              // 1.8446743e+19
}

// A conversion to f64 is written with `as` or not: an integer beyond 2^53
// that f64 cannot hold exactly rounds to the nearest f64, as float arithmetic
// rounds the results it computes.
fn f64_conversions() {
    // Read from arrays, so no value is known to the optimizer.
    var ints: i64[>..] = [9007199254740993, 9223372036854775807, -9223372036854775807];
    var wides: u64[>..] = [18446744073709551615, 9007199254740993, 9223372036854776833,
                           9223372036854778879, 9539625884325383047];

    print(ints[0] as f64);                  // 9007199254740992.0
    print(ints[1] as f64);                  // 9.223372036854776e+18
    print(ints[2] as f64);                  // -9.223372036854776e+18
    print(wides[0] as f64);                 // 1.8446744073709552e+19
    print(wides[1] as! f64);                // 9007199254740992.0
    // Past 2^63: 2^63 + 2^10 + 1 rounds up to 2^63 + 2^11, and
    // 2^63 + 3 * 2^10 - 1 down to it.
    print(wides[2] as f64, " ", wides[3] as f64);   // 9.223372036854778e+18 9.223372036854778e+18
    let big: f64 = wides[4];
    print(big, " ", wides[4] * 1.0);        // 9.539625884325382e+18 9.539625884325382e+18
    let d: f64 = ints[0];
    print(d);                               // 9007199254740992.0
    print(ints[0] * 1.0, " ", wides[0] + 0.5);  // 9007199254740992.0 1.8446744073709552e+19

    // Constants, which the optimizer folds.
    print(9007199254740993 as f64);         // 9007199254740992.0
    let c: f64 = 9007199254740993;
    print(c, " ", 18446744073709551615 * 1.0);  // 9007199254740992.0 1.8446744073709552e+19
    print(9223372036854776833 as f64);      // 9.223372036854778e+18
    let w: u64 = 9539625884325383047;
    let wd: f64 = w;
    print(wd);                              // 9.539625884325382e+18
}

fn main() {
    f32_conversions();
    f64_conversions();
}
