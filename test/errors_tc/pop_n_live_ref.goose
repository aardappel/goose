// error: cannot pop_n g while r is still used: it may hold a reference or slice into it
// A reference into a grow-only local is live at the pop_n (§5.1).
fn main() {
    var g: i64[>..] = [1, 2];
    let r = &g[0];
    g.pop_n(1);
    print(r);
}
