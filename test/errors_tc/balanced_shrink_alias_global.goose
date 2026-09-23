// error: cannot call f, which shrinks q while v
// error: is still used: it may refer into q
// f is balanced for its parameter's array, not for the global it clears,
// and that global is what q is here (§5.2).
var G: i64[>..<] = [1, 2, 3];
fn f(p: i64[>..<]&) {
    let mark = p.len;
    p.push(0);
    p.resize(mark);
    G.clear();
}
fn g(q: i64[>..<]&) -> i64 {
    let v = q[..];
    f(q);
    v[0]
}
fn main() {
    print(g(G));
}
