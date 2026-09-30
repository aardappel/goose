// error: cannot clear out while g is still used
// g merges h, a copy of an element hs names, but the store later in the
// loop body puts a view of out into h, which the next iteration merges
// (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, hs: H[:], c: bool) {
    var h = hs[0];
    for i in 0..2 {
        let g = if c { h } else { hs[1] };
        out.clear();
        if i == 0 { out.append("ZZZZZZZZ"); } else { out.append("Q"); }
        print(g.s);
        h.s = out[..];
    }
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: "x" });
    hs.push(H { s: "y" });
    emit(buf, hs[..], hs.len > 1);
}
