// error: cannot pop g while r is still used: it may hold a reference or slice into it
// A reference into a grow-only local is live at the pop (§5.1).
fn main() {
    var g: i64[>..] = [1, 2];
    let r = &g[0];
    let last = g.pop();
    print(r + last);
}
