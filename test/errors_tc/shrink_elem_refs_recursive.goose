// error: cannot call walk: it shrinks a while v is still used, and v may refer into a
// The deepest call clears a, and every level reads v's elements, views of
// a, after it: the first call finds them in ss's record (§5.1).
recursive fn walk(a: u8[>..]&, v: (u8[:])[:], n: i64) {
    if n == 0 {
        a.clear();
        a.append("ZZZZZZZZ");
        return;
    }
    walk(a, v, n - 1);
    print(v[0]);
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    walk(a, ss[..], 2);
}
