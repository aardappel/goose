// error: g while w is still used: it may hold a reference or slice into it
// The same where a function the block calls clears the global (§5.1, §7.6).
var g: i64[>..] = [1, 2];
fn drop_g() -> i64 {
    g.clear();
    return 0;
}
fn apply<F>() -> i64 {
    let w = g[..];
    let n = F();
    return w.len + n;
}
fn main() {
    print(apply() { drop_g() });
}
