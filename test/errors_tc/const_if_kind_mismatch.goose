// error: const if condition compares a string with an integer
struct S { x: i64 }
fn main() { const for <N, A> in fields<S> { const if N == 0 { print(N); } } }
