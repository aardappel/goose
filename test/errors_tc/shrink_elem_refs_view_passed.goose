// error: cannot call mid: it shrinks buf while ws is still used, and ws may refer into buf
// mid passes its slot on to f, which clears out and reads what the slot's
// slice views: main's s views names, whose element views buf (§5.1).
fn f(out: u8[>..]&, ws: ((u8[:])[:])&) {
    out.clear();
    out.append("ZZZZZZZZ");
    print(ws[0]);
}
fn mid(out: u8[>..]&, ws: ((u8[:])[:])&) { f(out, ws); }
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (u8[:])[>..] = [];
    names.push(buf[..]);
    var s = names[..];
    mid(buf, s);
}
