// error: returning a reference rooted in s, which dies with this function
// error: in const for field S.x
struct S { x: i64 }
fn escaped() -> i64& {
    var s = S { 1 };
    const for <N, A> in fields<S> { return s.A; }
    return s.x;
}
fn main() { print(escaped()); }
