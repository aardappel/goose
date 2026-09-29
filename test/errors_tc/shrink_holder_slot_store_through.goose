// error: cannot clear a while rr is still used: a reference to a slice that may point into it was stored there
// rr's field refers to s, which views b when rr is built; the write through
// the field makes s view a (§3.8), so what rr leads to is a's storage when
// the clear frees it (§5.1).
struct R { r: u8[:]& }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var s = b[..];
    var rr = R { r: s };
    rr.r = a[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(rr.r);
}
