// runtime-debug: a loaded owner is uncertain, but its sibling regions remain distinct.
root struct S { first: i64[>..<], second: i64[>..<] }
struct H { s: S& }
fn use(h: H&, s: S&) {
    let r .= h.s.first[0];
    s.second.clear();
    print(r);
}
fn head(h: H&) -> i64& { h.s.first[0] }
fn qualifier(s: S&, c: const S&) -> i64 {
    let r .= c.first[0];
    return s.first.index_of(r);
}
fn main() {
    var a = S { [1], [2] };
    var b = S { [3], [4] };
    var h = H { a };
    use(h, a);
    let r .= head(h);
    a.second.push(5);
    a.second.clear();
    print(r, " ", qualifier(a, a));
    h.s .= b;
    use(h, a);
}
