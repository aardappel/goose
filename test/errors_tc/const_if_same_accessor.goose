// error: const for binding A is not a type
struct S { x: i64 }
fn main() { const for <N, A> in fields<S> { const if same<A, i64> { print(N); } } }
