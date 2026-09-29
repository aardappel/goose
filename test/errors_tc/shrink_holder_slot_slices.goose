// error: cannot clear a while rr is still used: a reference to a slice that may point into it was stored there
// rr's field views ss's elements, slices the push made views of a (§5.1).
struct R2 { v: (u8[:])[:] }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    let rr = R2 { v: ss[..] };
    a.clear();
    a.append("ZZZZZZZZ");
    print(rr.v[0]);
}
