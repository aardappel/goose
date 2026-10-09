// An f64's text is the fewest significant digits that read back as the same
// f64 (§3.7): 15 or fewer where they do, else 16, else 17, and a subnormal as
// few as it needs. The values come from an array at run time, so the
// runtime's formatting is what prints them; the sums in the last line are
// folded by the compiler, whose text of a double follows the same rule.

fn main() {
    var xs: f64[>..] = [0.1, 1.5, 1.000000000000001, 0.3333333333333333, 123456789012345.6,
                        4.35, 5e-324, 1e-310, 2.2250738585072014e-308, 1.7976931348623157e308,
                        1e20, 100.0, 0.30000000000000004];
    for x in xs { print(x); }
    print(xs[0] + xs[0] * 2.0, " ", xs[1] / 3.0, " ", -xs[11] * 0.0);
    print(0.1 + 0.2, " ", 1.0 / 3.0, " ", 2.0 / 3.0);
}
