// error: cannot call walk: it shrinks a while w is still used, and w may refer into a
// Each level hands w on as v, and the deepest clears a: the level handed
// tt as v reads its element, a view of a, after it (§5.1).
recursive fn walk(a: u8[>..]&, v: (const u8[:])[:], w: (const u8[:])[:], n: i64) {
    if n == 0 {
        a.clear();
        a.append("ZZZZZZZZ");
        return;
    }
    walk(a, w, v, n - 1);
    print(v[0]);
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (const u8[:])[>..] = [];
    ss.push("q");
    var tt: (const u8[:])[>..] = [];
    tt.push(a[..]);
    walk(a, ss[..], tt[..], 2);
}
