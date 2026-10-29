// error: still used
root struct S { first: i64[>..<], second: i64[>..<] }
fn head(s: S&) -> i64& { s.first[0] }
fn clear(s: S&) { s.first.clear(); }
fn relay(s: S&) { clear(s); }
fn main() {
    var s = S { [1], [2] };
    let r .= head(s);
    relay(s);
    print(r);
}
