// error: cannot clear a while rr is still used: a reference to a slice that may point into it was stored there
// rr's field refers to h's slice field, which h's stores say views b; the
// write through rr's field stores a view of a there (§3.8), so what rr leads
// to is a's storage when the clear frees it (§5.1).
struct H { s: u8[:] }
struct R { r: u8[:]& }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var h = H { s: b[..] };
    var rr = R { r: h.s };
    rr.r = a[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(rr.r);
}
