// error: cannot call emit: it shrinks buf while c is still used, and c may refer into buf
// c refers into what a view the elements words views hold views, which is
// buf (§5.1).
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    let c = &words[0][1];
    out.clear();
    out.append("ZZZZZZZZ");
    print(c);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    emit(buf, names[..]);
}
