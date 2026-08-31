// A resizable field must be the final field (§3.4).
struct S {
    tail: i64[>..],
    after: i64,
}
fn main() { }
