// error: cannot call mid: it shrinks buf while p is still used, and p may refer into buf
// emit's pair about what p's class holds passes through mid, which hands its
// own p on, to main, where names' element views buf (§5.1).
struct P { words: (const u8[:])[:] }
fn emit(out: u8[>..]&, p: P) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(p.words[0]);
}
fn mid(out: u8[>..]&, p: P) {
    emit(out, p);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    mid(buf, P { words: names[..] });
}
