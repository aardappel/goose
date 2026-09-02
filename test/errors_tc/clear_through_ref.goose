// A grow-only array reached through a reference: the callee cannot see what
// the caller still points into (§5.1).
fn wipe(b: u8[>..]&) { b.clear(); }
fn main() {
    var buf: u8[>..] = [];
    buf.push(1);
    wipe(&buf);
    print(buf.len);
}
