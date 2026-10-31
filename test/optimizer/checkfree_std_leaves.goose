// checkfree (§7.14) through the standard library's scalar functions: they
// hold no index, so they are checkfree and a checkfree function may call
// them. float_text::bits_of reads a bytes_of view of an f64[1], whose length
// is the constant 8.
import std;
import math;

checkfree fn mix(x: f64, y: f64, n: i64, u: u32) -> f64 {
    let h = hash_combine(hash(x), hash(u)) ^ hash(true) ^ hash(n) ^ hash(x as f32);
    let s = sign(n) + rem(n, 3) + div_euclid(n, 4) + clamp(n, 0, 9) + min(n, 2) + max(n, 1) +
            abs(n);
    if is_nan(x) || is_inf(x) || !is_finite(y) { return 0.0; }
    let z = (h & 1) as f64 + s + next_pow2(n + 20) + degrees(radians(y));
    return max(abs(x), y) + lerp(x, y, 0.5) + sign(x) + z;
}

fn main() {
    print(mix(-3.0, 1.0, -7, 5));
    print(float_text::bits_of(1.0));
}
