// error: has_default<A> requires A to be a fields<T> accessor binding in scope
struct S { x: i64 = 1 }
fn main() { const for <N, A> in fields<S> { let A = 1; const if has_default<A> { print(A); } } }
