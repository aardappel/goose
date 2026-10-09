// runtime-debug: `/` and `>>` of a nonnegative value by a nonnegative divisor
// or any count lie between 0 and that value (§6.2), so an index halved from
// one in range stays in range. A signed `/`, `%` or `>>` the facts show to be
// of a nonnegative value, by a positive divisor, is computed unsigned; the
// others keep the signed, Euclidean and checked forms.
import std;

fn halves(xs: i64[:]) -> i64 {
    var s = 0;
    for i in xs.len {
        s += xs[i / 2];      // bce:elide
        s += xs[i >> 1];     // bce:elide
        s += xs[i / 3];      // bce:elide
    }
    return s;
}

// A difference the facts order is nonnegative.
fn midpoints(xs: i64[:]) -> i64 {
    var s = 0;
    for hi in xs.len {
        let lo = hi / 2;
        s += (hi - lo) / 2 + (hi - lo) % 3 + ((hi - lo) >> 1);
    }
    return s;
}

// Unknown signs keep the signed forms: truncating `/`, Euclidean `%`, and
// the arithmetic `>>`.
fn signed_ops(a: i64, b: i64) -> i64 {
    return a / b * 1000000 + a % b * 1000 + (a >> 1);
}

// A divisor only known to be nonnegative may still be zero and is checked.
fn by_count(n: i64, d: i64) -> i64 {
    var s = 0;
    for i in n {
        if d >= 0 { s += i / d; }
    }
    return s;
}

fn main() {
    let xs: i64[] = [1, 2, 4, 8, 16, 32, 64];
    print(halves(xs[..]), " ", midpoints(xs[..]));
    print(signed_ops(-7, 2), " ", signed_ops(7, -2), " ", signed_ops(-9, 4));
    let narrow: i32 = 37;
    var t: i32 = 0;
    for k: i32 in 0..narrow { t += k / 4 + k % 5 + (k >> 2); }
    print(t);
    print(by_count(5, 2));
    print(by_count(5, 0));
}
