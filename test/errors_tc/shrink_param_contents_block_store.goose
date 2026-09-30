// error: cannot call each: it shrinks buf while words is still used, and words may refer into buf
// The block each runs stores a view of buf into names while each is
// checked, before the call judges what names holds (§5.1).
fn each<F>(out: u8[>..]&, words: (const u8[:])[:]) {
    F();
    out.clear();
    out.append("ZZZZZZZZ");
    print(words[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    each(buf, names[..]) { names[0] = buf[..]; };
}
