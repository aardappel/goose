// error: varint fields are written only at construction
struct S { n: varint }
fn main() {
    var s = S { 2 };
    const for <N, A> in fields<S> { s.A = 3; }
}
