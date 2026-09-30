// error: cannot call f: it shrinks buf while p is still used, and p may refer into buf
// p refers to the caller's ss, which views buf: the call finds it by ss's
// record (§5.1).
struct S { r: const u8[:] }
struct P { q: S& }
fn f(out: u8[>..]&, p: P) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(p.q.r);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var ss = S { r: buf[..] };
    f(buf, P { q: ss });
}
