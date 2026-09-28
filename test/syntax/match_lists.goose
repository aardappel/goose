// dump-runtime: an integer match arm may list several values and ranges
// (§8.1), literal or named, bare or qualified, negated, and at the top of
// their type; any one of them selects the arm. Each is checked as a lone
// pattern is, per specialization of a generic function. A constant
// scrutinee selects the arm a later value names, and BCE bounds the
// scrutinee by the hull of an arm's values.
let GROUND = 0;
const SAND: u8 = 2;
let LO = 20;
let HI = 25;
let GATE = 70;
let NEG = -3;
let TOP: u64 = 18446744073709551615;

fn name(t: i64) -> u8[] {
    match t {
        GROUND, 1, SAND => "low",
        3..5, 7, 10..12 => "mixed",
        LO..HI, ::GATE => "named",
        NEG, -10..-5, -GATE => "negative",
        5, 6 => "five or six",
        _ => "other",
    }
}

fn top(x: u64) -> i64 {
    match x { TOP, 0..3 => 1, 5, 9223372036854775807..9223372036854775810 => 2, _ => 3 }
}

fn small(x: u8) -> i64 { match x { 255, 0 => 1, 10..20, 100 => 2, _ => 0 } }

fn either(x) -> i64 { match x { GROUND, 5 => 1, 2..4, 9 => 2, _ => 0 } }

fn at(k: i64) -> i64 {
    let a: i64[8] = [3, 5, 7, 9, 11, 13, 15, 17];
    match k {
        0, 2..4, 7 => a[k],                     // bce:elide
        1, 5..9 => a[k],                        // bce:keep
        _ => 0,
    }
}

fn main() {
    let ts: i64[22] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 20, 24, 25, 70, -3,
                       -10, -6, -5, -70, 71];
    for t in ts { print(t, " ", name(t)); }
    print(match 9 { 1, 2 => "a", 6..8, 9 => "b", _ => "c" }, " ",
          match 8 { 1, 2 => "a", 6..8, 9 => "b", _ => "c" }, " ",
          match GATE { 1, 2 => "a", LO..HI, GATE => "b", _ => "c" });   // b c b
    let us: u64[8] = [18446744073709551615, 0, 2, 3, 5, 9223372036854775807,
                      9223372036854775809, 9223372036854775810];
    var r = 0;
    for u in us { r = r * 10 + top(u); }
    print(r);                                                       // 11132223
    print(top(9223372036854775809), top(18446744073709551615), top(4));   // 213
    let bs: u8[7] = [255, 0, 254, 10, 19, 20, 100];
    r = 0;
    for b in bs { r = r * 10 + small(b); }
    print(r);                                                       // 1102202
    let is: i64[6] = [0, 5, 3, 9, 4, 1];
    let ws: u8[6] = [0, 5, 3, 9, 4, 1];
    r = 0;
    for i in is { r = r * 10 + either(i); }
    for w in ws { r = r * 10 + either(w); }
    print(r);                                                       // 112200112200
    let ks: i64[9] = [0, 1, 2, 3, 4, 5, 6, 7, 10];
    r = 0;
    for k in ks { r = r * 100 + at(k); }
    print(r);                                                       // 30507090013151700
}
