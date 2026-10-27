// error: in const for field S.flag
// error: operator + cannot be applied to bool and i64
struct S { flag: bool }
fn visit<T, F>(s: T&) { const for <N, A> in fields<T> { F(s.A); } }
fn main() {
    let s = S { true };
    visit(s) { v => print(v + 1) }
}
