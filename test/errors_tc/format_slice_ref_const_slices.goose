// error: cannot write through this reference: non-writable provenance
// A slice rendered as an element of a slice printed lies where that slice
// points (§3.7): a format hook taking it by reference gets a reference as
// writable as the slice, a read-only one for a `const` slice (§9.5).
var buf: u8[>..] = [];
fn format(out: u8[>..]&, p: u8[:]&) { out.append("Q"); p = buf[1..]; }
fn main() {
    buf.append("xyz");
    var arr: u8[:][>..] = [];
    arr.push(buf[..]);
    const ss = arr[..];
    print(ss);
}
