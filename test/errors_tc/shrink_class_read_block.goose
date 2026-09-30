// error: cannot call emit: it shrinks buf while w is still used, and w may refer into buf
// The block emit gives run reads w out of the elements words views, where
// names holds a view of buf (§5.1).
fn run<F>() { F(); }
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    run() {
        let w = words[0];
        out.clear();
        out.append("ZZZZZZZZ");
        print(w);
    };
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    emit(buf, names[..]);
}
