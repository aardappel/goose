// error: cannot call f: it shrinks buf while ws is still used, and ws may refer into buf
// ws's slot holds a slice of names, whose element views buf: f leaves what
// that slice's elements hold to its callers, and this call gives buf for
// out (§5.1).
fn f(out: u8[>..]&, ws: ((u8[:])[:])&) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(ws[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (u8[:])[>..] = [];
    names.push(buf[..]);
    var s = names[..];
    f(buf, s);
}
