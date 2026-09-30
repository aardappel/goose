// error: cannot call mid: it shrinks buf while hs is still used, and hs may refer into buf
// emit's pair about what the elements hs views hold passes through mid,
// which passes its own hs, to main, where the element views buf (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, hs: H[:]) {
    let h = hs[0];
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.s);
}
fn mid(out: u8[>..]&, hs: H[:]) { emit(out, hs); }
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: buf[..] });
    mid(buf, hs[..]);
}
