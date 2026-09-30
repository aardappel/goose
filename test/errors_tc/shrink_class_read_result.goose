// error: cannot call emit: it shrinks buf while w is still used, and w may refer into buf
// first returns a view it read out of the elements its parameter views,
// which words views, and names holds a view of buf there (§5.1).
fn first(ws: (const u8[:])[:]) -> const u8[:] { return ws[0]; }
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    let w = first(words);
    out.clear();
    out.append("ZZZZZZZZ");
    print(w);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    emit(buf, names[..]);
}
