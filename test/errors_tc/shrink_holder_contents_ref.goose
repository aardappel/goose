// error: cannot call f: it shrinks buf while h is still used, and h may refer into buf
// h refers to the holder q names, the caller's ss, which views buf (§5.1).
struct S { r: const u8[:] }
struct Q { s: S& }
fn f(out: u8[>..]&, q: S&) {
    let h = Q { s: q };
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.s.r);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var ss = S { r: buf[..] };
    f(buf, ss);
}
