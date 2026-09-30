// error: cannot call clr, which shrinks out: words[0], evaluated earlier in the statement, may still refer into out
// emit stored a view of out where the held view is read from (§5.1).
fn clr(out: u8[>..]&) -> i64 {
    out.clear();
    out.append("ZZZZZZZZ");
    return 0;
}
fn show(w: const u8[:], n: i64) { print(w, " ", n); }
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    words[0] = out[..];
    show(words[0], clr(out));
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    emit(buf, names[..]);
}
