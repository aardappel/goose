// A signed `/`, `%` or `>>` the facts show to be of a nonnegative value, by
// a positive divisor, is computed unsigned (Binary::nonneg), but only where
// the operands are the values the machine computed: `i + 1` of an i64 known
// only to be at least 0 wraps to i64.min in a release build (§6.2), and the
// signed operations then give a negative half, a Euclidean remainder and an
// arithmetic shift; a divisor `d + 2` wraps to a negative one.
fn halves(i: i64) -> i64, i64, i64 {
    if i < 0 { return 0, 0, 0; }
    return (i + 1) / 2, (i + 1) % 3, (i + 1) >> 1;
}

fn by_wrapped(x: i64, d: i64) -> i64 {
    if x < 0 || d < 0 { return 0; }
    return x / (d + 2);
}

fn main() {
    let xs: i64[] = [5, 9223372036854775807];
    for x in xs {
        let h, m, s = halves(x);
        print(h, " ", m, " ", s, " ", by_wrapped(x, x));
    }
}
