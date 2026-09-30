// error: cannot call emit: it shrinks buf while q is still used, and q may refer into buf
// q holds a copy of the element hs views, and the caller's holder there
// views buf (§5.1).
struct H { s: const u8[:] }
struct Q { h: H, n: i64 }
fn emit(out: u8[>..]&, hs: H[:]) {
    let q = Q { h: hs[0], n: 1 };
    out.clear();
    out.append("ZZZZZZZZ");
    print(q.h.s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: buf[..] });
    emit(buf, hs[..]);
}
