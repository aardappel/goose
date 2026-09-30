// error: cannot clear out while ws is still used: the slice it refers to may point into it
// f stores a view of out into the elements the slice in ws's slot views,
// and reads it after the clear (§5.1).
fn f(out: u8[>..]&, ws: ((const u8[:])[:])&) {
    ws[0] = out[..];
    out.clear();
    out.append("ZZZZZZZZ");
    print(ws[0]);
}
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var names: (const u8[:])[>..] = [];
    names.push("x");
    var s = names[..];
    f(buf, s);
}
