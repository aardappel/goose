// error: cannot call emit: it shrinks buf while hs is still used, and hs may refer into buf
// The holders hs views, read out of the elements wss views, hold a view of
// buf (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, wss: (H[:])[:]) {
    let hs = wss[0];
    out.clear();
    out.append("ZZZZZZZZ");
    print(hs[0].s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: buf[..] });
    var outer: (H[:])[>..] = [];
    outer.push(hs[..]);
    emit(buf, outer[..]);
}
