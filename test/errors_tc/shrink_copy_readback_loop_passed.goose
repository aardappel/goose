// error: cannot clear out while s is still used
// A nested function the loop calls after the shrink stores what it is
// passed, a view of out, into h, which the next iteration reads (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, hs: H[:]) {
    var h = hs[0];
    fn keep(v: const u8[:]) { h.s = v; }
    for i in 0..2 {
        let s = h.s;
        out.clear();
        if i == 0 { out.append("ZZZZZZZZ"); } else { out.append("Q"); }
        print(s);
        keep(out[..]);
    }
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: "x" });
    emit(buf, hs[..]);
}
