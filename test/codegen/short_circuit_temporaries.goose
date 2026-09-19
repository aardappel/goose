// The right operand of && and || runs only when the left one lets it, and so
// do the restores of the temporaries it builds on data stacks: a call's
// variable-size result compared with a slice, a str() result, a literal of
// variable-size elements. Each is compared with the operand running and
// skipped, nested, and in a loop condition; the scratch array grown after
// each call lands where those temporaries were.
struct Txt { body: u8[] }

fn word(long: bool) -> u8[] {
    if long { return "abcdef"; }
    return "ab";
}

fn scratch() -> i64 {
    var s: i64[>..] = [];
    for i in 100 { s.push(i); }
    var sum = 0;
    for x in s { sum += x; }
    return sum;
}

fn either(xs: u8[:], long: bool) -> bool { return xs.len > 3 || word(long) == xs; }
fn both(xs: u8[:], long: bool) -> bool { return xs.len < 3 && word(long) == xs; }
fn named(n: i64) -> bool { return n > 5 || str("n", n) == "n3"; }
fn texts(ts: Txt[], n: i64) -> bool {
    return n == 0 || ts == [Txt { body: "a" }, Txt { body: "bc" }];
}
fn nested(xs: u8[:], n: i64) -> bool {
    return n > 9 || (xs.len > 0 && (word(n > 4) == xs || str(n) == xs));
}

fn main() {
    print(either("ab", false), " ", either("ab", true), " ", either("abcd", true), " ",
          scratch());
    print(both("ab", false), " ", both("ab", true), " ", both("abcd", false), " ", scratch());
    print(named(3), " ", named(4), " ", named(6), " ", scratch());
    print(texts([Txt { body: "a" }, Txt { body: "bc" }], 1), " ",
          texts([Txt { body: "a" }], 1), " ", texts([Txt { body: "a" }], 0), " ", scratch());
    print(nested("abcdef", 5), " ", nested("7", 7), " ", nested("", 3), " ",
          nested("x", 10), " ", nested("x", 3), " ", scratch());
    var w: u8[>..] = [];
    while w.len < 9 && word(true) != w { w.push(('a' + w.len) as u8); }
    print(w, " ", scratch());
}
