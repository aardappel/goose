// error: cannot clear out while h is still used: a reference into it was stored there
// emit itself stored a view of out where it copies h from (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, hs: H[:]) {
    hs[0] = H { s: out[..] };
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
