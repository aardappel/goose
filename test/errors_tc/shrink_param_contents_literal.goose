// error: cannot call emit: it shrinks buf while words is still used, and words may refer into buf
// The literal's second element views buf, which emit clears before it
// reads the element (§5.1).
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(words[1]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    emit(buf, ["y", buf[..]]);
}
