// error: cannot call emit: it shrinks buf while h is still used, and h may refer into buf
// h is a copy of one of the elements hs views, and the caller's holder in
// the second views buf (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, hs: H[:], c: bool) {
    let h = if c { hs[0] } else { hs[1] };
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: "x" });
    hs.push(H { s: buf[..] });
    emit(buf, hs[..], buf.len > 5);
}
