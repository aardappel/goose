// error: cannot clear g while w (bound at
// error: is still used: it may refer into g
// The block names main's w after it clears g; apply's w is another
// variable (§5.2, §7.6).
var g: i64[>..<] = [1, 2];
fn apply<F>() -> i64 {
    let n = F();
    let w = 7;
    return n + w;
}
fn main() {
    let w = g[..];
    print(apply() { g.clear(); w.len });
}
