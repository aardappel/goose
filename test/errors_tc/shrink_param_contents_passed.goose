// error: cannot call mid: it shrinks buf while words is still used, and words may refer into buf
// mid hands its own parameters on: the pair reaches main, where names'
// element views buf (§5.1).
fn emit(out: u8[>..]&, words: (u8[:])[:]) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(words[0]);
}
fn mid(out: u8[>..]&, words: (u8[:])[:]) {
    emit(out, words);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (u8[:])[>..] = [];
    names.push(buf[..]);
    mid(buf, names[..]);
}
