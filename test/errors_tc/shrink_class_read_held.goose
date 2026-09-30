// error: cannot call emit: it shrinks buf while words[0] is still used, and words[0] may refer into buf
// The view read out of the elements words views is held while clr clears
// out, and names holds a view of buf there (§5.1).
fn clr(out: u8[>..]&) -> i64 {
    out.clear();
    out.append("ZZZZZZZZ");
    return 0;
}
fn show(w: const u8[:], n: i64) { print(w, " ", n); }
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    show(words[0], clr(out));
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    emit(buf, names[..]);
}
