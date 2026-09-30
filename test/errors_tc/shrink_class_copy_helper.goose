// error: cannot call emit: it shrinks buf while h is still used, and h may refer into buf
// clr clears out while emit's copy of the element hs views is still used,
// and that element views buf (§5.1).
struct H { s: const u8[:] }
fn clr(out: u8[>..]&) {
    out.clear();
    out.append("ZZZZZZZZ");
}
fn emit(out: u8[>..]&, hs: H[:]) {
    let h = hs[0];
    clr(out);
    print(h.s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: buf[..] });
    emit(buf, hs[..]);
}
