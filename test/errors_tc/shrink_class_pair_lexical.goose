// error: cannot call f: it may shrink ys while s is still used, and s may refer into ys
// The nested function pops one of f's parameters while viewing the other,
// and f is given h.a, which may be ys (§9.5), beside ys itself (§5.1).
struct H { a: i64[>..]& }
fn f(a: i64[>..]&, b: i64[>..]&) {
    fn g() {
        let s = b[..];
        a.pop();
        a.push(77);
        print(s[2]);
    }
    g();
}
fn main() {
    var ys: i64[>..] = [1, 2, 3];
    var zs: i64[>..] = [4];
    let h = H { a: ys };
    f(h.a, ys);
}
