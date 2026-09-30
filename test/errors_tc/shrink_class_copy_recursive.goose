// error: cannot clear out while h is still used: a reference into it was stored there
// The deepest level stores a view of out where the levels above copy h
// from after it returns (§5.1).
struct H { s: const u8[:] }
recursive fn walk(out: u8[>..]&, hs: H[:], n: i64) {
    if n == 0 {
        hs[0] = H { s: out[..] };
        return;
    }
    walk(out, hs, n - 1);
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
    walk(buf, hs[..], 1);
}
