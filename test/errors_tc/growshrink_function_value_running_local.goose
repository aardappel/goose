// error: cannot pop g while w (bound at
// error: is still used: it may refer into g
// The same for a local of the function running the block, a view of the
// global the block pops (§5.2, §7.6).
var g: i64[>..<] = [1, 2];
fn apply<F>() -> i64 {
    let w = g[..];
    let n = F();
    return w[1] + n;
}
fn main() {
    print(apply() { g.pop(); g.push(99); 0 });
}
