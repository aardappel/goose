// error: still used
root struct S { first: i64[>..<], second: i64[>..<] }
struct H { s: S& }
fn use(h: H&, s: S&) {
    let r .= h.s.first[0];
    s.first.clear();
    print(r);
}
fn main() { var s = S { [1], [2] }; var h = H { s }; use(h, s); }
