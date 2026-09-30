// error: cannot call emit: it shrinks buf while h is still used, and h may refer into buf
// h is a copy of p, whose words view names' elements, and one views buf
// (§5.1).
struct P { words: (const u8[:])[:] }
fn emit(out: u8[>..]&, p: P) {
    let h = p;
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.words[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    emit(buf, P { words: names[..] });
}
