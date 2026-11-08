// error: cannot clear buf while c is still used: it may hold a reference or slice into it
// A chunk of a grow-only array views its elements, as a slice does (§5.1).
fn main() {
    var buf: i64[>..] = [1, 2, 3, 4];
    let c = buf.chunk(1, 2);
    buf.clear();
    print(c);
}
