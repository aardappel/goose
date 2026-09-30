// error: cannot clear out while s is still used
// The loop's next iteration reads the view of out a callee stores into h
// later in its body, after the shrink (§5.1).
struct H { s: const u8[:] }
fn put(h: H&, s: const u8[:]) { h.s = s; }
fn emit(out: u8[>..]&, hs: H[:]) {
    var h = hs[0];
    for i in 0..2 {
        let s = h.s;
        out.clear();
        if i == 0 { out.append("ZZZZZZZZ"); } else { out.append("Q"); }
        print(s);
        put(h, out[..]);
    }
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: "x" });
    emit(buf, hs[..]);
}
