// error: cannot clear out while s is still used
// q holds h, a copy of an element hs names, but the store later in the loop
// body puts a view of out into h, which the next iteration copies into q
// (§5.1).
struct H { s: const u8[:] }
struct Q { h: H, n: i64 }
fn emit(out: u8[>..]&, hs: H[:]) {
    var h = hs[0];
    for i in 0..2 {
        let q = Q { h: h, n: i };
        let s = q.h.s;
        out.clear();
        if i == 0 { out.append("ZZZZZZZZ"); } else { out.append("Q"); }
        print(s);
        h.s = out[..];
    }
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: "x" });
    emit(buf, hs[..]);
}
