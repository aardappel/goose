// error: fields requires a concrete struct or variant type
enum E { V { x: i64 } }
fn main() { const for <N, A> in fields<E> {} }
