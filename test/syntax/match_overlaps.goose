// dump-runtime: match patterns may overlap as long as each can still
// select its arm (§8.1). An earlier arm takes special cases out of a later
// range, ranges meet end to end, a u64's largest value is matched before a
// range below it, and a u8 match covers every value and still ends in _.
// Within one arm, differently named constants may be one value's aliases,
// and a literal may lie in a named range. A generic function's `_` may be
// left no variant for one enum and some for another.
let EAGAIN = 11;
let EWOULDBLOCK = 11;
let EINTR = 4;
let LO = 20;
let HI = 30;
let TOP: u64 = 18446744073709551615;

enum Two { A, B }
enum Three { A, B, C }

fn errno(e: i64) -> u8[:] {
    match e { EAGAIN, EWOULDBLOCK => "retry", EINTR => "interrupted", _ => "fail" }
}

fn count(n: i64) -> u8[:] {
    match n {
        0 => "none",
        1 => "one",
        0..10 => "few",
        10..LO => "some",
        LO..HI, 25 => "score",
        _ => "many",
    }
}

fn edge(u: u64) -> i64 { match u { TOP => 1, 9223372036854775807..18446744073709551615 => 2, _ => 3 } }

fn full(b: u8) -> i64 { match b { 0..128 => 1, 128..255 => 2, 255 => 3, _ => 0 } }

fn pair(x) -> i64 { match x { A, B => 1, _ => 0 } }

fn main() {
    print(errno(11), " ", errno(4), " ", errno(5));                  // retry interrupted fail
    let ns: i64[8] = [0, 1, 2, 9, 10, 19, 25, 30];
    for n in ns { print(n, " ", count(n)); }
    let us: u64[4] = [18446744073709551615, 18446744073709551614, 9223372036854775807, 5];
    var r = 0;
    for u in us { r = r * 10 + edge(u); }
    print(r);                                                       // 1223
    let bs: u8[4] = [0, 127, 128, 255];
    r = 0;
    for b in bs { r = r * 10 + full(b); }
    print(r);                                                       // 1123
    print(pair(Two.A), pair(Two.B), pair(Three.B), pair(Three.C));  // 1110
}
