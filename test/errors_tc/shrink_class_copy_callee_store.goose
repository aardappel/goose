// error: cannot clear out while h is still used: a reference into it was stored there
// put stores a view of out into the elements hs views before emit copies h
// out of them (§5.1).
struct H { s: const u8[:] }
fn put(hs: H[:], s: const u8[:]) { hs[0] = H { s: s }; }
fn emit(out: u8[>..]&, hs: H[:]) {
    put(hs, out[..]);
    let h = hs[0];
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: "x" });
    emit(buf, hs[..]);
}
