// error: negated constant too large for i64
// A literal above i64.max is a u64 constant (§6.2), and of those only 2^63
// negates, to exactly i64.min: one more does not.
fn main() {
    print(-9223372036854775809);
}
