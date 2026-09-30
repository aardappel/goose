// error: cannot clear out while h is still used: a reference into it was stored there
// emit itself stored a view of out in one of the elements h may be a copy
// of (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, hs: H[:], c: bool) {
    hs[1] = H { s: out[..] };
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
    hs.push(H { s: "y" });
    emit(buf, hs[..], buf.len > 5);
}
