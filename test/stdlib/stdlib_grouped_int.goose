// runtime-debug: format_grouped writes the digits of any i64 in groups of three, and parse_grouped_int reads that back,
// refusing groups of the wrong size instead of dropping the separators blindly.
import std;

fn show(s: u8[:], sep: u8[:] = "'") {
    let v, ok = parse_grouped_int(s, sep);
    print(v, " ", ok);
}

fn grouped(v: i64, sep: u8[:] = "'") {
    var out: u8[>..] = [];
    format_grouped(out, v, sep);
    print(out);
}

fn main() {
    grouped(0);                                        // 0
    grouped(999);                                      // 999
    grouped(1000);                                     // 1'000
    grouped(10000000);                                 // 10'000'000
    grouped(-123456);                                  // -123'456
    grouped(-9223372036854775807 - 1);                 // -9'223'372'036'854'775'808
    grouped(1234567, ",");                             // 1,234,567
    grouped(1234567, "’");                             // 1’234’567

    var small: u8[..16] = [];
    format_grouped(small, 1234567, " ");
    print(small);                                      // 1 234 567

    show("10'000'000");                                // 10000000 true
    show("-1'000");                                    // -1000 true
    show("+999");                                      // 999 true
    show("1000");                                      // 1000 true
    show("1,234,567", ",");                            // 1234567 true
    show("1’234’567", "’");                            // 1234567 true
    show("9'223'372'036'854'775'807");                 // 9223372036854775807 true
    show("-9'223'372'036'854'775'808");                // -9223372036854775808 true
    show("9'223'372'036'854'775'808");                 // 0 false
    show("1'0'0");                                     // 0 false
    show("1000'000");                                  // 0 false
    show("1'0000");                                    // 0 false
    show("'100");                                      // 0 false
    show("100'");                                      // 0 false
    show("1''000");                                    // 0 false
    show("-'100");                                     // 0 false
    show("1'00x");                                     // 0 false
    show("");                                          // 0 false
}
