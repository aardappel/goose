// Integer match arms at the ends of their type (§8.1): a single-value arm
// may be the type's largest value, which a range's exclusive end cannot
// name, and one at either end matches only that value. Each function runs
// on values read out of an array, compared at run time, and on literal
// arguments, which -O2 inlines into a constant scrutinee that selects its
// arm at compile time; both lines of a type print the same digits. BCE takes
// an arm's bounds as facts about the scrutinee, its last value included.

fn s64(x: i64) -> i64 {
    match x {
        9223372036854775807 => 1,
        -9223372036854775808 => 2,
        9223372036854775800..9223372036854775807 => 3,
        _ => 0,
    }
}

fn u64s(x: u64) -> i64 {
    match x {
        18446744073709551615 => 1,
        0 => 2,
        10..18446744073709551615 => 3,
        _ => 0,
    }
}

fn u8s(x: u8) -> i64 { match x { 255 => 1, 0 => 2, 200..255 => 3, _ => 0 } }

fn i8s(x: i8) -> i64 { match x { 127 => 1, -128 => 2, -127..0 => 3, _ => 0 } }

fn at(k: i64) -> i64 {
    let a: i64[4] = [3, 5, 7, 9];
    match k {
        0..3 => a[k],                         // bce:elide
        3 => a[k],                            // bce:elide
        _ => 0,
    }
}

fn past(k: i64, a: i64[4]) -> i64 {
    match k {
        2..5 => a[k],                         // bce:keep
        _ => 0,
    }
}

fn main() {
    let si: i64[6] = [9223372036854775807, -9223372036854775807 - 1, 9223372036854775806,
                      9223372036854775800, 9223372036854775799, 0];
    var r = 0;
    for x in si { r = r * 10 + s64(x); }
    print(r);                                                           // 123300
    print(s64(9223372036854775807), s64(-9223372036854775807 - 1), s64(9223372036854775806),
          s64(9223372036854775800), s64(9223372036854775799), s64(0));  // 123300
    let su: u64[5] = [18446744073709551615, 18446744073709551614, 0, 9, 10];
    r = 0;
    for x in su { r = r * 10 + u64s(x); }
    print(r);                                                           // 13203
    print(u64s(18446744073709551615), u64s(18446744073709551614), u64s(0), u64s(9),
          u64s(10));                                                    // 13203
    let s8: u8[5] = [255, 254, 0, 199, 200];
    r = 0;
    for x in s8 { r = r * 10 + u8s(x); }
    print(r);                                                           // 13203
    print(u8s(255), u8s(254), u8s(0), u8s(199), u8s(200));              // 13203
    let si8: i8[6] = [127, -128, -127, -1, 0, 126];
    r = 0;
    for x in si8 { r = r * 10 + i8s(x); }
    print(r);                                                           // 123300
    print(i8s(127), i8s(-128), i8s(-127), i8s(-1), i8s(0), i8s(126));   // 123300
    print(at(0), at(2), at(3), at(4), at(-1));                          // 37900
    let a: i64[4] = [3, 5, 7, 9];
    let ks: i64[3] = [2, 3, 1];
    r = 0;
    for k in ks { r = r * 10 + past(k, a); }
    print(r);                                                           // 790
}
