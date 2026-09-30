// error: cannot clear out while words is still used: the slices it views may point into it
// f itself stores a view of out into the elements words views (§5.1).
fn f(out: u8[>..]&, words: (const u8[:])[:]) {
    words[0] = out[..];
    out.clear();
    out.append("ZZZZZZZZ");
    print(words[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    f(buf, names[..]);
}
