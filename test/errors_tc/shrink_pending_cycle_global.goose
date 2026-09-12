// A recursive call counts as shrinking what every function still being
// checked textually shrinks: the back edge into b holds g[0], and a, which
// b calls further down, clears g.
var g: i64[>..] = [1, 2];
fn read(r: i64&, unused: i64) -> i64 { return r; }
recursive fn a(n: i64) -> i64 {
    if n == 0 {
        g.clear();
        g.push(99);
        return 0;
    }
    let z = b(n);
    return z;
}
recursive fn b(n: i64) -> i64 {
    if n > 1 { return read(g[0], b(n - 1)); }
    let z = a(0);
    return z;
}
fn main() {
    let v = a(2);
    print(v);
}
