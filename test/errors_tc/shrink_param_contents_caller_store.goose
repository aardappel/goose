// error: cannot call emit: it shrinks out while words is still used, and words may refer into out
// mid stores a view of out into what words views, then hands both to
// emit, which clears one and reads the other (§5.1).
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(words[0]);
}
fn mid(out: u8[>..]&, words: (const u8[:])[:]) {
    words[0] = out[..];
    emit(out, words);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    mid(buf, names[..]);
}
