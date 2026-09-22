// error: cannot call walk: it shrinks xs while s is still used, and s may refer into xs
// The entry call passes walk two arrays, and its back edge one: a slice of
// xs as s, which the body the back edge reuses pops xs under (§7.8, §5.1).
recursive fn walk(xs: i64[>..]&, s: i64[:], n: i64) {
    xs.pop();
    xs.push(70 + n);
    print(s[2]);
    if n > 0 { walk(xs, xs[..], n - 1); }
}
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5, 6];
    walk(a, b[..], 1);
}
