// error: cannot call emit: it shrinks buf while p is still used, and p may refer into buf
// p's words view names, whose element views buf: emit leaves what those
// elements hold to its callers, and this call gives buf for out (§5.1).
struct P { words: (const u8[:])[:] }
fn emit(out: u8[>..]&, p: P) {
    out.clear();
    out.append("ZZZZZZZZ");
    for w in p.words { print(w); }
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    emit(buf, P { words: names[..] });
}
