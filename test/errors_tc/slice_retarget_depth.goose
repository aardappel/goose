// error: re-binding s with a reference rooted at a different scope depth is not supported
// A slice variable keeps its first binding's scope depth (§9.2): one bound
// to a string literal takes views of globals, not of a local array (§3.10).
fn main() {
    var name: u8[..8] = "n";
    var s = "x";
    s = name;
    print(s);
}
