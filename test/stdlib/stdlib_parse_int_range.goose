// runtime-debug: parse_int reads every i64, including the most negative one, and refuses what is outside i64 instead of
// wrapping; valid input must not trip GS_DEBUG's overflow check inside std.
import std;

fn show(s: u8[:], base: i64 = 10) {
    let v, ok = parse_int(s, base);
    print(v, " ", ok);
}

fn main() {
    show("-9223372036854775808");                      // -9223372036854775808 true
    show("9223372036854775807");                       // 9223372036854775807 true
    show("+9223372036854775807");                      // 9223372036854775807 true
    show("-9223372036854775807");                      // -9223372036854775807 true
    show("9223372036854775808");                       // 0 false
    show("-9223372036854775809");                      // 0 false
    show("99999999999999999999");                      // 0 false
    show("-99999999999999999999");                     // 0 false
    show("-8000000000000000", 16);                     // -9223372036854775808 true
    show("7fffffffffffffff", 16);                      // 9223372036854775807 true
    show("8000000000000000", 16);                      // 0 false
    show("0000000000000000000009223372036854775807");  // 9223372036854775807 true
    show("-0000000000000000000009223372036854775808"); // -9223372036854775808 true
    show("-0");                                        // 0 true
    show("-");                                         // 0 false
    show("12x");                                       // 0 false
}
