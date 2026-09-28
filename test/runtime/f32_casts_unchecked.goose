// runtime-debug: a conversion to f32 never aborts, in a debug build either
// (§6.3). It rounds to the nearest f32, from an f64 or from any integer, and
// beyond f32's range it is an infinity.
fn main() {
    // Read from arrays, so no value is known to the optimizer.
    var doubles: f64[>..] = [0.1, 1e300, -1e300, 16777217.0];
    var ints: i64[>..] = [16777217, 123456789, 9223372036854775807, 1152921573326323713];
    var halves: i32[>..] = [2147483647];
    var words: u32[>..] = [4294967295];
    var wides: u64[>..] = [18446744073709551615];

    let a = doubles[0] as f32;
    print(a);                               // 0.10000000149011612
    let up = doubles[1] as f32;
    print(up > 3.4e38, " ", up == up * 2.0);            // true true
    let down = doubles[2] as f32;
    print(down < -3.4e38, " ", down == down * 2.0);     // true true
    print(doubles[3] as f32);               // 16777216
    print(ints[0] as f32);                  // 16777216
    print(ints[1] as f32);                  // 123456792
    print(ints[2] as f32);                  // 9.2233720368547758e+18
    // 2^60 + 2^36 + 1 rounds up to 2^60 + 2^37. Through f64 it would first
    // round to the tie 2^60 + 2^36, and from there down to 2^60.
    print(ints[3] as f32);                  // 1.1529216420458004e+18
    print(halves[0] as f32);                // 2147483648
    print(words[0] as f32);                 // 4294967296
    print(wides[0] as f32);                 // 1.8446744073709552e+19
    // Into a wider slot, the value is still the rounded one.
    let d: f64 = doubles[0] as f32;
    print(d);                               // 0.10000000149011612
    print((doubles[0] as f32) == (doubles[0] as! f32));  // true

    // Constants, which the optimizer folds.
    print(0.1 as f32);                      // 0.10000000149011612
    let cup = 1e300 as f32;
    print(cup > 3.4e38, " ", cup == cup * 2.0);         // true true
    print(16777217 as f32);                 // 16777216
    print(9223372036854775807 as f32);      // 9.2233720368547758e+18
    print(1152921573326323713 as f32);      // 1.1529216420458004e+18
    print(18446744073709551615 as f32);     // 1.8446744073709552e+19
}
