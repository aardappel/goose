// error: cannot call emit: it shrinks buf while h is still used, and h may refer into buf
// The block emit gives run copies a holder out of the elements hs views,
// where the caller's holder views buf (§5.1).
struct H { s: const u8[:] }
fn run<F>() { F(); }
fn emit(out: u8[>..]&, hs: H[:]) {
    run() {
        let h = hs[0];
        out.clear();
        out.append("ZZZZZZZZ");
        print(h.s);
    };
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: buf[..] });
    emit(buf, hs[..]);
}
