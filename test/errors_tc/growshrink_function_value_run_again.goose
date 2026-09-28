// error: cannot pop g while w (bound at
// error: is still used: it may refer into g
// twice runs the block again after it pops g, and the block reads main's
// view of g before its pop (§5.2, §7.6).
fn twice<F>() {
    F();
    F();
}
fn main() {
    var g: i64[>..<] = [1, 2];
    let w = g[..];
    twice() { print(w); g.pop(); g.push(9); }
}
