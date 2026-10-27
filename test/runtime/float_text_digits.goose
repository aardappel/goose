// An f64's text is the fewest significant digits that read back as the same
// f64 (§3.7): 15 or fewer where they do, else 16, else 17, and a subnormal as
// few as it needs. The values come from an array at run time, so the
// runtime's formatting is what prints them; the sums in the last print below
// are folded by the compiler, whose text of a double follows the same rule.
fn fewest_digits() {
    var xs: f64[>..] = [0.1, 1.5, 1.000000000000001, 0.3333333333333333, 123456789012345.6,
                        4.35, 5e-324, 1e-310, 2.2250738585072014e-308, 1.7976931348623157e308,
                        1e20, 100.0, 0.30000000000000004];
    for x in xs { print(x); }
    print(xs[0] + xs[0] * 2.0, " ", xs[1] / 3.0, " ", -xs[11] * 0.0);
    print(0.1 + 0.2, " ", 1.0 / 3.0, " ", 2.0 / 3.0);
}

// A float's text is the nearest of the fewest digits that read back as the
// same value of its type (§3.7). Below a power of two the gap to the next
// float is half the gap above, so that text can lie above the value while the
// nearest spelling of its length, below it, reads back as another float:
// 2^89 is 618970019642690137449562112, and its text is 6.189700196426902e+26,
// though 6.189700196426901e+26 is nearer. Of two spellings equally near, the
// one ending in an even digit is the text, whatever the C library does with
// a tie. Every value is computed at run time, so the runtime prints it.

// 2^k, by halving or doubling 1.
fn pow2(k: i64) -> f64 {
    var p = 1.0;
    var i = k;
    while i < 0 {
        p = p * 0.5;
        i += 1;
    }
    while i > 0 {
        p = p * 2.0;
        i -= 1;
    }
    return p;
}

fn nearest_digits() {
    var ks: i64[>..] = [-1017, -662, -140, -97, -77, -44, -24, 89, 122, 976];
    for k in ks { print(k, " ", pow2(k), " ", -pow2(k)); }
    var ks32: i64[>..] = [-96, 87, 90];
    for k in ks32 { print(k, " ", pow2(k) as f32, " ", -pow2(k) as f32); }
    // Halfway between two 16-digit and two 8-digit spellings that read back.
    print(pow2(49) + 0.25, " ", pow2(49) + 0.75);
    print((pow2(21) + 0.25) as f32, " ", (pow2(21) + 0.75) as f32);
}

fn main() {
    fewest_digits();
    nearest_digits();
}
