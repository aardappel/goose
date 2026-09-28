// error: cannot clear g while w (bound at
// error: is still used: it may refer into g
// apply's own w is used after the block it runs clears g, whatever names
// the block gives its own variables (§5.2, §7.6).
var g: i64[>..<] = [1, 2];
fn apply<F>() -> i64 {
    let w = g[..];
    let n = F();
    return w.len + n;
}
fn main() {
    print(apply() { g.clear(); let w = 5; w });
}
