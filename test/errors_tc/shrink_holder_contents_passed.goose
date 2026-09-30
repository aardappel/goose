// error: cannot call mid: it shrinks buf while p is still used, and p may refer into buf
// emit's pair about what p's class holds passes through mid, which reads p
// after the call, to main, where names' element views buf (§5.1).
struct P { words: (const u8[:])[:] }
fn emit(out: u8[>..]&, p: P) {
    out.clear();
    out.append("ZZZZZZZZ");
}
fn mid(out: u8[>..]&, p: P) {
    emit(out, p);
    print(p.words[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    mid(buf, P { words: names[..] });
}
