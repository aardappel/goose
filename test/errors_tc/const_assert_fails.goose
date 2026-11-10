// error: fill copies v into every element, which a T that holds own values does not allow
// A const assert whose condition is false in a specialization fails the
// compile with its message, at the call that made the specialization
// (§6.5).
struct Buf { n: i64 }
struct Conn { id: i64, input: own Buf? }
fn fill<T>(dst: T[:], v: T&) -> i64 {
    const assert !holds_own<T>, "fill copies v into every element, which a T that holds own values does not allow";
    return dst.len;
}
fn main() {
    var cs = [Conn { id: 1, input: null }];
    print(fill(cs, cs[0]));
}
