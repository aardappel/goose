// error: cannot call mid: it shrinks buf while q is still used, and q may refer into buf
// emit leaves what q's holder holds to mid, where hq holds mid's words:
// mid leaves what those view to main, where names' element views buf
// (§5.1).
struct P { words: (const u8[:])[:] }
struct Q { p: P }
fn emit(out: u8[>..]&, q: Q&) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(q.p.words[0]);
}
fn mid(out: u8[>..]&, words: (const u8[:])[:]) {
    var hq = Q { p: P { words: words } };
    emit(out, hq);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    mid(buf, names[..]);
}
