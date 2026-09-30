// error: cannot call f: it shrinks buf while words is still used, and words may refer into buf
// put stores a view of out through w2, which may view names too: names'
// record says so once the call maps f's stores (§5.1).
fn put(w2: (const u8[:])[:], x: u8[>..]&) { w2[0] = x[..]; }
fn f(out: u8[>..]&, words: (const u8[:])[:], w2: (const u8[:])[:]) {
    put(w2, out);
    out.clear();
    out.append("ZZZZZZZZ");
    print(words[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    var others: (const u8[:])[>..] = [];
    others.push("y");
    f(buf, names[..], if buf.len > 1 { names[..] } else { others[..] });
}
