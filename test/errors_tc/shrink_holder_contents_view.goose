// error: cannot call f: it shrinks buf while h is still used, and h may refer into buf
// h holds the slice in ws's slot, which views names, and names' element
// views buf: the call maps the slice's class to what s held (§5.1).
struct P { words: (const u8[:])[:] }
fn f(out: u8[>..]&, ws: ((const u8[:])[:])&) {
    let h = P { words: ws };
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.words[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    var s = names[..];
    f(buf, s);
}
