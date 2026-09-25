// error: cannot assign through this path (const, or a read-only instantiation
// An if's value is a temporary copy, as read-only as a call's result: a
// write would update the copy and nothing else (§9.5).
var g: i64[3] = [1, 2, 3];
var h: i64[3] = [4, 5, 6];
fn main() {
    let c = g[0] < h[0];
    (if c { g } else { h })[0] = 7;
    print(g[0], " ", h[0]);
}
