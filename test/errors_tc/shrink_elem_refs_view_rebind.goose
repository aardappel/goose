// error: cannot call f: it shrinks buf while ws is still used, and ws may refer into buf
// f rebinds the slot to a slice of o, whose element views the array out
// names: the call gives buf for out (§5.1).
fn f(out: u8[>..]&, ws: ((const u8[:])[:])&, others: (const u8[:])[:]) {
    ws = others;
    out.clear();
    out.append("ZZZZZZZZ");
    print(ws[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    var o: (const u8[:])[>..] = [];
    o.push(buf[..]);
    var s = names[..];
    f(buf, s, o[..]);
}
