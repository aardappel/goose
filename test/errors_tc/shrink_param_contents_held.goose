// error: cannot call f: it shrinks buf while words is still used, and words may refer into buf
// show is handed words before clr clears out, and reads its element after
// (§5.1).
fn clr(a: u8[>..]&) -> i64 {
    a.clear();
    a.append("ZZZZZZZZ");
    return 0;
}
fn show(v: (u8[:])[:], n: i64) { print(v[0]); }
fn f(out: u8[>..]&, words: (u8[:])[:]) {
    show(words, clr(out));
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (u8[:])[>..] = [];
    names.push(buf[..]);
    f(buf, names[..]);
}
