// error: cannot clear out while w is still used
// w was read out of a slice itself read out of the elements wss views:
// that slice only bounds the elements w lies in (§5.1).
fn emit(out: u8[>..]&, wss: ((const u8[:])[:])[:]) {
    let ws = wss[0];
    let w = ws[0];
    out.clear();
    out.append("ZZZZZZZZ");
    print(w);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push(buf[..]);
    var outer: ((const u8[:])[:])[>..] = [];
    outer.push(names[..]);
    emit(buf, outer[..]);
}
