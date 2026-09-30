// error: cannot clear out while h is still used: a reference into it was stored there
// first stores a view of out in the element it returns a copy of, as part
// of emit's activation (§5.1).
struct H { s: const u8[:] }
fn first(hs: H[:], s: const u8[:]) -> H {
    hs[0] = H { s: s };
    return hs[0];
}
fn emit(out: u8[>..]&, hs: H[:]) {
    let h = first(hs, out[..]);
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
