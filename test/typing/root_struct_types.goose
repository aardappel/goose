// Root declarations keep their ownership category regardless of their fields.
root struct Empty {}
root struct Counter { n: i64 }
root struct Buffer { a: i64[>..] }
root struct State<T> { a: T[>..], n: i64, b: T[>..<] }
struct Borrowed { state: State<i64>? }
fn main() { var empty = Empty {}; var counter = Counter { 7 }; print(counter.n); }
