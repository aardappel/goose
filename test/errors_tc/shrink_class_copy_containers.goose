// error: cannot clear out while h is still used: a reference into it was stored there
// hs views a's elements or b's: a copy out of it came out of neither
// storage alone (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, a: H[:], b: H[:], c: bool) {
    let hs = if c { a } else { b };
    for h in hs {
        out.clear();
        out.append("ZZZZZZZZ");
        print(h.s);
    }
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var x: H[>..] = [];
    x.push(H { s: "x" });
    var y: H[>..] = [];
    y.push(H { s: buf[..] });
    emit(buf, x[..], y[..], buf.len > 5);
}
