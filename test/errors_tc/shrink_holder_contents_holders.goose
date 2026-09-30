// error: cannot call f: it shrinks buf while p is still used, and p may refer into buf
// p's slice views the caller's holders, the first of which views buf
// (§5.1).
struct H { s: const u8[:] }
struct P { hs: H[:] }
fn f(out: u8[>..]&, p: P) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(p.hs[0].s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hl: H[>..] = [];
    hl.push(H { s: buf[..] });
    f(buf, P { hs: hl[..] });
}
