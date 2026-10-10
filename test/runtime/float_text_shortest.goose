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

fn main() {
    var ks: i64[>..] = [-1017, -662, -140, -97, -77, -44, -24, 89, 122, 976];
    for k in ks { print(k, " ", pow2(k), " ", -pow2(k)); }
    var ks32: i64[>..] = [-96, 87, 90];
    for k in ks32 { print(k, " ", pow2(k) as f32, " ", -pow2(k) as f32); }
    // Halfway between two 16-digit and two 8-digit spellings that read back.
    print(pow2(49) + 0.25, " ", pow2(49) + 0.75);
    print((pow2(21) + 0.25) as f32, " ", (pow2(21) + 0.75) as f32);
}
