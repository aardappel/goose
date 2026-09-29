// error: cannot clear a while t is still used: it may hold a reference or slice into it
// take's result views what cur holds, a's elements, which the clear frees
// while t is still used (§5.1).
fn take(p: u8[:]&, n: i64) -> u8[:] {
    let r = p[0..n];
    p = p[n..];
    return r;
}
fn main() {
    var a: u8[>..] = [];
    a.append("hello world");
    var cur = a[..];
    let t = take(cur, 5);
    a.clear();
    print(t);
}
