// error: cannot call emit: it shrinks buf while h is still used, and h may refer into buf
// h keeps a view read out of the elements words views, and names holds a
// view of buf there (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    let h = H { s: words[0] };
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    emit(buf, names[..]);
}
