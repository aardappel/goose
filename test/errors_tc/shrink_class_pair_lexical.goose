// error: cannot call f: it may shrink ys while s is still used, and s may refer into ys
// The nested function pops one of f's parameters while viewing the other,
// and f is given h.a, read back out of a copy of an element of hs, which
// may be ys (§9.5), beside ys itself (§5.1).
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
    let hs = [H { a: ys }, H { a: zs }];
    let h = hs[0];
    f(h.a, ys);
}
