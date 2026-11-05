// error: const if condition s names a local variable, not a constant
struct S { x: i64 }
fn main() { var s = S { 1 }; const for <N, A> in fields<S> { const if s.A == 0 { print(N); } } }
