// error: cannot clear a while p is still used: what it refers to may hold a reference or slice into it
// p refers to rr, whose field refers to s, which views a: two references
// on from p, a's storage is what the clear frees (§5.1).
struct R { r: u8[:]& }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var rr = R { r: s };
    let p = &rr;
    a.clear();
    a.append("ZZZZZZZZ");
    print(p.r);
}
