// error: cannot call f, which shrinks c while t is still used: it may hold a reference or slice into it
// The recursive call's base case stores c's view into the slot it is given,
// t (§3.8, §7.8): t may view c when the call clears it and after (§5.1).
var b: u8[>..] = [];
var c: u8[>..] = [];
recursive fn f(p: u8[:]&, n: i64) {
    if n == 0 {
        p = c[..];
        return;
    }
    let t = b[..];
    f(t, n - 1);
    c.clear();
    c.append("QQQ");
    print(t);
}
fn main() {
    b.append("xyz");
    c.append("abc");
    var s = b[..];
    f(s, 2);
}
