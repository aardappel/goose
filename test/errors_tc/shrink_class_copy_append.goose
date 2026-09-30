// error: cannot call f: it shrinks buf while keep is still used, and keep may refer into buf
// keep copies the holders hs views, one of which views buf (§5.1).
struct H { s: const u8[:] }
fn f(out: u8[>..]&, hs: H[:]) {
    var keep: H[>..] = [];
    keep.append(hs);
    out.clear();
    out.append("ZZZZZZZZ");
    print(keep[0].s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: buf[..] });
    f(buf, hs[..]);
}
