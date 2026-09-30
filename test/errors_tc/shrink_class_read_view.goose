// error: cannot call f: it shrinks buf while w is still used, and w may refer into buf
// w was read out of the elements the slice in ws's slot views, where names
// holds a view of buf (§5.1).
fn f(out: u8[>..]&, ws: ((const u8[:])[:])&) {
    let w = ws[0];
    out.clear();
    out.append("ZZZZZZZZ");
    print(w);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    var s = names[..];
    f(buf, s);
}
