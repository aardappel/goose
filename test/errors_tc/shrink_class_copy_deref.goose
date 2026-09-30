// error: cannot call f: it shrinks buf while h is still used, and h may refer into buf
// h is a copy of the holder r names, which views buf (§5.1).
struct H { s: const u8[:] }
fn f(out: u8[>..]&, r: H&) {
    let h: H = r;
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hh = H { s: buf[..] };
    f(buf, hh);
}
