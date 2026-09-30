// error: cannot clear out while w is still used
// emit itself stored a view of out where it reads w from (§5.1).
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    words[0] = out[..];
    let w = words[0];
    out.clear();
    out.append("ZZZZZZZZ");
    print(w);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    emit(buf, names[..]);
}
