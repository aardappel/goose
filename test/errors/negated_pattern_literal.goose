// error: negated literal too large for i64
// A match pattern's negated literal follows the expression rule (§6.2): of
// the u64 literals, only 2^63 negates, to exactly i64.min.
fn main() {
    let x = 5;
    match x {
        -9223372036854775809 => print("below"),
        _ => print("other"),
    }
}
