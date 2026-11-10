// runtime-debug: parse_uint reads every u64, including those above i64.max, and refuses what is outside u64 instead of
// wrapping; valid input must not trip GS_DEBUG's overflow check inside std.
import std;

fn show(s: u8[:], base: i64 = 10) {
    let v, ok = parse_uint(s, base);
    print(v, " ", ok);
}

fn main() {
    show("18446744073709551615");                                                   // 18446744073709551615 true
    show("+18446744073709551615");                                                  // 18446744073709551615 true
    show("18446744073709551616");                                                   // 0 false
    show("18446744073709551620");                                                   // 0 false
    show("184467440737095516150");                                                  // 0 false
    show("99999999999999999999");                                                   // 0 false
    show("9223372036854775807");                                                    // 9223372036854775807 true
    show("9223372036854775808");                                                    // 9223372036854775808 true
    show("12345678901234567890");                                                   // 12345678901234567890 true
    show("0000000000000000000018446744073709551615");                               // 18446744073709551615 true
    show("1111111111111111111111111111111111111111111111111111111111111111", 2);    // 18446744073709551615 true
    show("10000000000000000000000000000000000000000000000000000000000000000", 2);   // 0 false
    show("1777777777777777777777", 8);                                              // 18446744073709551615 true
    show("2000000000000000000000", 8);                                              // 0 false
    show("ffffffffffffffff", 16);                                                   // 18446744073709551615 true
    show("FFFFFFFFFFFFFFFF", 16);                                                   // 18446744073709551615 true
    show("10000000000000000", 16);                                                  // 0 false
    show("8000000000000000", 16);                                                   // 9223372036854775808 true
    show("3w5e11264sgsf", 36);                                                      // 18446744073709551615 true
    show("3W5E11264SGSG", 36);                                                      // 0 false
    show("Zz", 36);                                                                 // 1295 true
    show("1", 2);                                                                   // 1 true
    show("2", 2);                                                                   // 0 false
    show("7", 8);                                                                   // 7 true
    show("8", 8);                                                                   // 0 false
    show("9");                                                                      // 9 true
    show("a");                                                                      // 0 false
    show("f", 16);                                                                  // 15 true
    show("g", 16);                                                                  // 0 false
    show("0", 0);                                                                   // 0 false
    show("1", -16);                                                                 // 0 false
    show("0");                                                                      // 0 true
    show("+0");                                                                     // 0 true
    show("-0");                                                                     // 0 false
    show("-1");                                                                     // 0 false
    show("");                                                                       // 0 false
    show("+");                                                                      // 0 false
    show("++1");                                                                    // 0 false
    show(" 1");                                                                     // 0 false
    show("1 ");                                                                     // 0 false
    show("1_000");                                                                  // 0 false
    show("0x10");                                                                   // 0 false
    show("12x");                                                                    // 0 false
    show("10", 1);                                                                  // 0 false
    show("10", -1);                                                                 // 0 false
    show("10", 37);                                                                 // 0 false
    show("10", 64);                                                                 // 0 false
    show("ff", 36);                                                                 // 555 true
    show("ff", 37);                                                                 // 0 false
}
