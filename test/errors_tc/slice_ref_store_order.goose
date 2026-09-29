// error: cannot call clear_g, which shrinks g while y is still used: it may hold a reference or slice into it
// The second iteration stores into y what the first stored into x, a view
// of g, though the body loads x before storing into it: the call assigns y
// all that x's slot holds at any point of it (§3.8), so y may view g (§5.1).
var g: u8[>..] = [];
var h: u8[>..] = [];
fn f(p: u8[:]&, q: u8[:]&) {
    for i in 2 {
        q = p[..];
        p = g[..];
    }
}
fn clear_g() {
    g.clear();
    g.append("QQQ");
}
fn main() {
    g.append("xyz");
    h.append("ab");
    let x = h[..];
    let y = h[0..1];
    f(x, y);
    clear_g();
    print(y);
}
