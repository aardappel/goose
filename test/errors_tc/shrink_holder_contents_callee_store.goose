// error: cannot clear out while h is still used: a reference to a slice that may point into it was stored there
// put stores a view of out into the elements words views, which h holds
// (§5.1).
struct P { words: (const u8[:])[:] }
fn put(ws: (const u8[:])[:], s: const u8[:]) { ws[0] = s; }
fn f(out: u8[>..]&, words: (const u8[:])[:]) {
    let h = P { words: words };
    put(words, out[..]);
    out.clear();
    out.append("ZZZZZZZZ");
    print(h.words[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    f(buf, names[..]);
}
