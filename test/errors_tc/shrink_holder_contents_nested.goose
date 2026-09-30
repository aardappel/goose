// error: cannot call f: it shrinks buf while p is still used, and p may refer into buf
// g clears f's out and reads f's holder parameter: g's pair passes through
// f to main (§5.1).
struct P { words: (const u8[:])[:] }
fn f(out: u8[>..]&, p: P) {
    fn g() {
        out.clear();
        out.append("ZZZZZZZZ");
        print(p.words[0]);
    }
    g();
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    f(buf, P { words: names[..] });
}
