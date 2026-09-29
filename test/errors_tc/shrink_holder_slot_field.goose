// error: cannot clear a while rr is still used: a reference to a slice that may point into it was stored there
// rr's field refers to h's slice field (§4.1), which h's stores say views a
// (§5.1).
struct H { s: u8[:] }
struct R { r: u8[:]& }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var h = H { s: a[..] };
    var rr = R { r: h.s };
    a.clear();
    a.append("ZZZZZZZZ");
    print(rr.r);
}
