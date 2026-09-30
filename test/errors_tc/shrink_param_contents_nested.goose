// error: cannot call f: it shrinks buf while words is still used, and words may refer into buf
// g clears f's out and reads f's words: g's pair passes through f to main
// (§5.1).
fn f(out: u8[>..]&, words: (u8[:])[:]) {
    fn g() {
        out.clear();
        out.append("ZZZZZZZZ");
        print(words[0]);
    }
    g();
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (u8[:])[>..] = [];
    names.push(buf[..]);
    f(buf, names[..]);
}
