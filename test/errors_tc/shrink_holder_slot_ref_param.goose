// error: cannot call use: it shrinks GA while h is still used, and h may refer into GA
// h's field refers to a slot of the caller's, which may hold a view of GA:
// here s, which does, as the call finds by rr's record (§5.1).
struct R { r: u8[:]& }
var GA: u8[>..] = [];
fn use(h: R&) {
    GA.clear();
    GA.append("ZZZZZZZZ");
    print(h.r);
}
fn main() {
    GA.append("abc");
    var s = GA[..];
    var rr = R { r: s };
    use(rr);
}
