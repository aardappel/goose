// Two root classes of a function's parameters are two arrays wherever every
// call passes two (§5.1, §5.2): a shrink of one while a reference rooted at
// the other is still used stays legal when the calls say so, through a
// function passing its own classes on, around a recursive cycle, and beside
// temporaries or variables deeper than an inexact root.
struct H { a: i64[>..]& }
struct R { r: i64& }
fn g(xs: i64[>..]&, r: i64&) { xs.pop(); xs.push(70); print(r); }
fn view(xs: i64[>..]&, s: const i64[:]) { xs.pop(); xs.push(71); print(s[s.len - 1]); }
fn f(a: i64[>..]&, b: i64[>..]&) {
    g(a, b[0]);
    view(a, b[..]);
}
recursive fn walk(xs: i64[>..]&, ys: i64[>..]&, n: i64) {
    if n > 0 { walk(xs, ys, n - 1); }
    g(xs, ys[1]);
}
// Whatever h.a is, it is not a variable of this function.
fn own(a: i64[>..]&) {
    var ws: i64[>..] = [7, 8, 9];
    g(a, ws[2]);
}
fn mk() -> i64[] { return [5, 6]; }
fn holder(xs: i64[>..]&, r: i64&) {
    let k = R { r: r };
    let c = k;
    xs.pop();
    xs.push(72);
    print(c.r);
}
fn store(xs: i64[>..]&, ys: i64[>..]&) {
    var mine: i64[1] = [0];
    var k = R { r: mine[0] };
    for i in 2 {
        xs.pop();
        xs.push(80 + i);
        print(k.r);
        k = R { r: ys[0] };
    }
}
fn each(xs: i64[>..]&, s: i64[:]) {
    for x in s {
        xs.pop();
        xs.push(x);
    }
    print(xs);
}
fn lexical(a: i64[>..]&, b: i64[>..]&) {
    fn inner() {
        let s = b[..];
        a.pop();
        a.push(74);
        print(s[0]);
    }
    inner();
}
fn shrinks(xs: i64[>..<]&, r: i64&) { xs.pop(); xs.push(75); print(r); }
fn main() {
    var ys: i64[>..] = [1, 2, 3];
    var zs: i64[>..] = [4, 5, 6];
    g(ys, zs[1]);
    f(ys, zs);
    walk(ys, zs, 2);
    view(ys, [9, 8][..]);
    view(ys, mk()[..]);
    let h = H { a: ys };
    let a .= h.a;
    own(a);
    {
        var deep: i64[>..] = [10, 11];
        g(a, deep[1]);
    }
    holder(ys, zs[2]);
    store(ys, zs);
    each(ys, zs[..]);
    lexical(ys, zs);
    var q: i64[>..<] = [1, 2];
    var p: i64[>..<] = [3];
    shrinks(q, p[0]);
    print(ys, zs, q);
}
