// error: cannot call emit: it shrinks buf while words is still used, and words may refer into buf
// words' element views names2, whose element views buf: the call follows
// names' record on to names2's (§5.1).
fn emit(out: u8[>..]&, words: ((const u8[:])[:])[:]) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(words[0][0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names2: (const u8[:])[>..] = [];
    names2.push(buf[..]);
    var names: ((const u8[:])[:])[>..] = [];
    names.push(names2[..]);
    emit(buf, names[..]);
}
