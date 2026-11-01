// runtime-debug: gcd(i64.min, 0) is 2^63, which no i64 holds; it aborts in
// every build rather than wrap to a negative divisor.
import std;
fn main() {
    let mn = -9223372036854775807 - 1;
    print(gcd(mn, 2));
    print(gcd(mn, 0));
}
