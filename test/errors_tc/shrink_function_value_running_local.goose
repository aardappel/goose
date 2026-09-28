// error: cannot clear g while w is still used: it may hold a reference or slice into it
// The same for a local of the function running the block, a view of the
// global the block clears (§5.1, §7.6).
var g: i64[>..] = [1, 2];
fn apply<F>() -> i64 {
    let w = g[..];
    let n = F();
    return w.len + n;
}
fn main() {
    print(apply() { g.clear(); 0 });
}
