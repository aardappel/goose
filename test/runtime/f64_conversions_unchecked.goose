// runtime-debug: a conversion to f64 never aborts, in a debug build either
// (§6.3), written with `as` or not: an integer beyond 2^53 that f64 cannot
// hold exactly rounds to the nearest f64, as float arithmetic rounds the
// results it computes.
fn main() {
    // Read from arrays, so no value is known to the optimizer.
    var ints: i64[>..] = [9007199254740993, 9223372036854775807, -9223372036854775807];
    var wides: u64[>..] = [18446744073709551615, 9007199254740993];

    print(ints[0] as f64);                  // 9007199254740992.0
    print(ints[1] as f64);                  // 9.223372036854776e+18
    print(ints[2] as f64);                  // -9.223372036854776e+18
    print(wides[0] as f64);                 // 1.8446744073709552e+19
    print(wides[1] as! f64);                // 9007199254740992.0
    let d: f64 = ints[0];
    print(d);                               // 9007199254740992.0
    print(ints[0] * 1.0, " ", wides[0] + 0.5);  // 9007199254740992.0 1.8446744073709552e+19

    // Constants, which the optimizer folds.
    print(9007199254740993 as f64);         // 9007199254740992.0
    let c: f64 = 9007199254740993;
    print(c, " ", 18446744073709551615 * 1.0);  // 9007199254740992.0 1.8446744073709552e+19
}
