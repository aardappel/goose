// error: still used
root struct S { first: i64[>..<], second: i64[>..<] }
fn use(a: S&, b: S&) {
    let r .= a.first[0];
    b.first.clear();
    print(r);
}
fn main() { var s = S { [1], [2] }; use(s, s); }
