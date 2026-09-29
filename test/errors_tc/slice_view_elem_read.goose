// error: cannot clear text while w is still used: it may hold a reference or slice into it
// first loads an element out of the slice its parameter's slot holds, a
// slice words keeps, not that slice itself: w may view text, which the
// clear frees (§5.1).
fn first(p: (u8[:])[:]&) -> u8[:] { return p[0]; }
fn main() {
    var words: (u8[:])[>..] = [];
    var text: u8[>..] = [];
    text.append("hello");
    words.push(text[0..2]);
    var s = words[..];
    let w = first(s);
    text.clear();
    print(w);
}
