// error: string constants compare with == and != only in a const if condition
struct S { x: i64 }
fn main() { const for <N, A> in fields<S> { const if N < "b" { print(N); } } }
