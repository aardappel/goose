// Comparisons against a sum, and ranges with computed endpoints: `i + c`
// states a fact wherever the facts show that the addition did not wrap. The
// arrays are slices of an opaque length, so every fact comes from the
// function itself.
import std;

// The doubled-quote lookahead of a CSV scanner.
fn doubled(s: u8[:]) -> i64 {
    var n = 0;
    var i = 0;
    while i < s.len {
        if s[i] == '"' && i + 1 < s.len && s[i + 1] == '"' { // bce:elide
            n++;
            i += 2;
            continue;
        }
        i++;
    }
    return n;
}

// `while i + 3 <= len` steps through whole groups.
fn groups(xs: i64[:]) -> i64 {
    var t = 0;
    var i = 0;
    while i + 3 <= xs.len {
        t += xs[i] * 100 + xs[i + 1] * 10 + xs[i + 2]; // bce:elide
        i += 3;
    }
    return t;
}

// A range from `i + 1`: every later element.
fn later_pairs(xs: i64[:]) -> i64 {
    var t = 0;
    for i in xs.len {
        for j in i + 1..xs.len { t += xs[i] * xs[j]; } // bce:elide
    }
    return t;
}

// `a..e + 1` is the inclusive range up to e.
fn up_to_last(xs: i64[:]) -> i64 {
    guard xs.len > 0 else { return 0; }
    let e = xs.len - 1;
    var t = 0;
    for j in 1..e + 1 { t += xs[j]; } // bce:elide
    return t;
}

// With nothing bounding k from above, `k + 1` may wrap to a negative start.
fn unbounded_start(xs: i64[:], k: i64) -> i64 {
    guard k >= 0 else { return 0; }
    var t = 0;
    for j in k + 1..xs.len { t += xs[j]; } // bce:keep
    return t;
}

fn main() {
    var ks: i64[>..] = [11, 7, 1];
    var text: u8[>..] = [];
    text.append("a\"\"b\"c\"\"\"\"d");
    var nums: i64[>..] = [1, 2, 3, 4, 5, 6, 7, 8];
    let t = text[..ks[0]];
    let xs = nums[..ks[1]];
    print(doubled(t));            // 3
    print(groups(xs));            // 579
    print(later_pairs(xs));       // 322
    print(up_to_last(xs));        // 27
    print(unbounded_start(xs, ks[2])); // 25
}
