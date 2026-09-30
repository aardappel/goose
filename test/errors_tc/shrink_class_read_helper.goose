// error: cannot call emit: it shrinks buf while words is still used, and words may refer into buf
// show clears out while it uses the view emit read out of the elements
// words views and handed it, and names holds a view of buf there (§5.1).
fn show(out: u8[>..]&, w: const u8[:]) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(w);
}
fn emit(out: u8[>..]&, words: (const u8[:])[:]) {
    let w = words[0];
    show(out, w);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    emit(buf, names[..]);
}
