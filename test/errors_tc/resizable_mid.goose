// A resizable field must be the final field (§3.4).
struct S {
    tail: int[>..],
    after: int,
}
fn main() { }
