// error: cannot call drop, which shrinks a: a[..], evaluated earlier in the statement
// error: may still refer into a
// The slice passed first is still used by total when drop has cleared the
// grow-only array it views (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
fn total(s: const i64[:], n: i64) -> i64 {
    var t = n;
    for x in s { t += x; }
    return t;
}
fn main() {
    var a: i64[>..] = [1, 2];
    print(total(a[..], drop(a)));
}
