// error: cannot call emit: it shrinks buf while h is still used, and h may refer into buf
// h is the copy first returns of the element hs views, and the caller's
// holder there views buf (§5.1).
struct H { s: const u8[:] }
fn first(hs: H[:]) -> H { return hs[0]; }
fn emit(out: u8[>..]&, hs: H[:]) {
    let h = first(hs);
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: buf[..] });
    emit(buf, hs[..]);
}
