// error: cannot call emit: it shrinks buf while h is still used, and h may refer into buf
// The store later in the loop body puts a view of out where the next
// iteration copies h from (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, hs: H[:]) {
    for i in 0..2 {
        let h = hs[0];
        out.clear();
        out.append("ZZZZZZZZ");
        print(h.s);
        hs[0] = H { s: out[..] };
    }
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: "x" });
    emit(buf, hs[..]);
}
