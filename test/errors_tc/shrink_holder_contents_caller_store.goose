// error: cannot call emit: it shrinks out while q is still used, and q may refer into out
// mid stores a view of out into what words views, keeps words in hq and
// hands both to emit, which clears one and reads the other (§5.1).
struct P { words: (const u8[:])[:] }
struct Q { p: P }
fn emit(out: u8[>..]&, q: Q&) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(q.p.words[0]);
}
fn mid(out: u8[>..]&, words: (const u8[:])[:]) {
    words[0] = out[..];
    var hq = Q { p: P { words: words } };
    emit(out, hq);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    mid(buf, names[..]);
}
