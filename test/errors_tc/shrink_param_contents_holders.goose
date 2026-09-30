// error: cannot call emit: it shrinks buf while hs is still used, and hs may refer into buf
// The first holder hs views holds a view of buf (§5.1).
struct H { s: const u8[:], n: i64 }
fn emit(out: u8[>..]&, hs: H[:]) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(hs[0].s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hl: H[>..] = [];
    hl.push(H { s: buf[..], n: 1 });
    emit(buf, hl[..]);
}
