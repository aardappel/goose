// error: cannot clear out while q is still used: a reference into it was stored there
// emit itself stored a view of out in the element q holds a copy of
// (§5.1).
struct H { s: const u8[:] }
struct Q { h: H, n: i64 }
fn emit(out: u8[>..]&, hs: H[:]) {
    hs[0] = H { s: out[..] };
    let q = Q { h: hs[0], n: 1 };
    out.clear();
    out.append("ZZZZZZZZ");
    print(q.h.s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: "x" });
    emit(buf, hs[..]);
}
