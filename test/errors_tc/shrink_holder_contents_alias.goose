// error: cannot call f: it shrinks buf while p is still used, and p may refer into buf
// ws may name the array p's words view, and f stores a view of out through
// it: f cannot tell, the call can by names' record (§5.1).
struct P { words: (const u8[:])[:] }
fn f(out: u8[>..]&, p: P, ws: (const u8[:])[:]) {
    ws[0] = out[..];
    out.clear();
    out.append("ZZZZZZZZ");
    print(p.words[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    var other: (const u8[:])[>..] = [];
    other.push("y");
    let c = buf.len > 1;
    f(buf, P { words: names[..] }, if c { names[..] } else { other[..] });
}
