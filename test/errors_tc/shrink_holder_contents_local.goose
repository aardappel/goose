// error: cannot call emit: it shrinks buf while h is still used, and h may refer into buf
// h holds emit's words, which view names' elements, and one views buf
// (§5.1).
struct P { words: (const u8[:])[:] }
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    let h = P { words: words };
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.words[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    emit(buf, names[..]);
}
