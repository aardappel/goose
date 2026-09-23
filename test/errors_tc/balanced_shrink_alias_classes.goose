// error: cannot call f, which shrinks a while v
// error: is still used: it may refer into a
// f is balanced for its first parameter's array and clears its second's;
// two parameters of g are one array wherever a call passes it to both, as
// main does here through r (§5.2).
fn f(p: i64[>..<]&, r: i64[>..<]&) {
    let mark = p.len;
    p.push(0);
    p.resize(mark);
    r.clear();
}
fn g(a: i64[>..<]&, b: i64[>..<]&) -> i64 {
    let v = a[..];
    f(a, b);
    v[0]
}
fn main() {
    var s: i64[>..<] = [1, 2];
    var t: i64[>..<] = [3];
    var r: i64[>..<]& = t;
    if s.len > 1 { r .= s; }
    print(g(s, r));
}
