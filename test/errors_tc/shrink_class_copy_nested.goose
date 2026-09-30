// error: cannot call emit: it shrinks buf while h is still used, and h may refer into buf
// g copies a holder out of the elements emit's hs views and clears emit's
// out: g's pair passes through emit to main (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, hs: H[:]) {
    fn g() {
        let h = hs[0];
        out.clear();
        out.append("ZZZZZZZZ");
        print(h.s);
    }
    g();
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: buf[..] });
    emit(buf, hs[..]);
}
