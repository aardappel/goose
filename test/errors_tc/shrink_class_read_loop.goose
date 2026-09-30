// error: cannot call emit: it shrinks buf while w is still used, and w may refer into buf
// The store later in the loop body puts a view of out where the next
// iteration reads w from (§5.1).
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    for i in 0..2 {
        let w = words[0];
        out.clear();
        out.append("ZZZZZZZZ");
        print(w);
        words[0] = out[..];
    }
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    emit(buf, names[..]);
}
