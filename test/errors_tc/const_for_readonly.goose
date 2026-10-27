// error: cannot assign through this path (const, or a read-only instantiation
struct S { x: i64 }
fn main() {
    const s = S { 1 };
    const for <N, A> in fields<S> { s.A = 2; }
}
