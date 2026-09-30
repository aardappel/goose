// error: cannot call emit: it shrinks buf while h is still used, and h may refer into buf
// The for binder copies a holder out of the elements hs views, and the
// caller's holder there views buf (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, hs: H[:]) {
    for h in hs {
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
    emit(buf, hs[..]);
}
