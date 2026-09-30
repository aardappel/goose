// error: cannot clear out while p is still used: a reference to a slice that may point into it was stored there
// f itself stores a view of out into the elements p's words view (§5.1).
struct P { words: (const u8[:])[:] }
fn f(out: u8[>..]&, p: P) {
    p.words[0] = out[..];
    out.clear();
    out.append("ZZZZZZZZ");
    print(p.words[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    f(buf, P { words: names[..] });
}
