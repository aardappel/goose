// error: cannot call f: it shrinks buf while words is still used, and words may refer into buf
// The block f hands each clears out, and each reads words after running
// it (§5.1).
fn each<F>(out: u8[>..]&, words: (const u8[:])[:]) {
    F();
    print(words[0]);
}
fn f(out: u8[>..]&, words: (const u8[:])[:]) {
    each(out, words) {
        out.clear();
        out.append("ZZZZZZZZ");
    };
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    f(buf, names[..]);
}
