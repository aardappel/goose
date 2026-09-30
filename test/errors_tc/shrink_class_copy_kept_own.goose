// error: cannot clear out while h is still used: a reference into it was stored there
// emit itself stored a view of out where it reads the view h keeps from
// (§5.1).
struct H { s: const u8[:] }
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    words[0] = out[..];
    var h = H { s: "" };
    h.s = words[0];
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.s);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    emit(buf, names[..]);
}
