// error: cannot call emit: it shrinks buf while s is still used, and s may refer into buf
// s was read out of the holder h names, which holds a view of buf (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, h: H&) {
    let s = h.s;
    out.clear();
    out.append("ZZZZZZZZ");
    print(s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var h = H { s: buf[..] };
    emit(buf, h);
}
