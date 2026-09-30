// error: cannot call f: it shrinks buf while p is still used, and p may refer into buf
// pp's words view names, into whose element main stores a view of buf
// after making pp (§5.1).
struct P { words: (const u8[:])[:] }
fn f(out: u8[>..]&, p: P) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(p.words[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    let pp = P { words: names[..] };
    names[0] = buf[..];
    f(buf, pp);
}
