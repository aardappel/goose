// error: cannot call emit: it shrinks buf while w is still used, and w may refer into buf
// The for binder is one of the views the elements words views hold, and
// names holds a view of buf there (§5.1).
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    for w in words {
        out.clear();
        out.append("ZZZZZZZZ");
        print(w);
    }
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    emit(buf, names[..]);
}
