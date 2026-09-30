// error: cannot call f: it shrinks buf while h is still used, and h may refer into buf
// h was popped off the caller's array, whose element views buf (§5.1).
struct H { s: const u8[:] }
fn f(out: u8[>..]&, hs: H[>..]&) {
    let h = hs.pop();
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: buf[..] });
    f(buf, hs);
}
