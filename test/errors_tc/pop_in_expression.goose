// error: cannot pop g: &g[0], evaluated earlier in the statement
// error: may still refer into g
// A shrink of a grow-only local inside a larger expression (§5.1): the
// reference argument evaluated before the pop would outlive it.
fn use(r: i64&, v: i64) -> i64 { r + v }
fn main() {
    var g: i64[>..] = [1, 2];
    print(use(&g[0], g.pop()));
}
