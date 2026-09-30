// error: cannot call f: it shrinks buf while h is still used, and h may refer into buf
// h is a copy of an element of the slice in ws's slot, where the caller's
// holder views buf (§5.1).
struct H { s: const u8[:] }
fn f(out: u8[>..]&, ws: (H[:])&) {
    for h in ws {
        out.clear();
        out.append("ZZZZZZZZ");
        print(h.s);
    }
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: buf[..] });
    var s = hs[..];
    f(buf, s);
}
