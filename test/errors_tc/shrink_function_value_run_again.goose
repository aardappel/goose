// error: cannot pop g while w is still used: it may hold a reference or slice into it
// twice runs the block again after it pops g, and the block reads main's
// view of g before its pop (§5.1, §7.6).
fn twice<F>() {
    F();
    F();
}
fn main() {
    var g: i64[>..] = [1, 2];
    let w = g[..];
    twice() { print(w); g.pop(); g.push(9); }
}
