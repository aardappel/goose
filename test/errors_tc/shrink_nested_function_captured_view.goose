// error: cannot clear g while w is still used: it may hold a reference or slice into it
// A nested function's body is code of the function it is declared in: it
// names main's w after it clears g (§5.1, §7.5).
fn main() {
    var g: i64[>..] = [1, 2];
    let w = g[..];
    fn drop_and_count() -> i64 {
        g.clear();
        return w.len;
    }
    print(drop_and_count());
}
