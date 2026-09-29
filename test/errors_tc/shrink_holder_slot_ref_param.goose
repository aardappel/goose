// error: cannot clear GA while h is still used: what it refers to may hold a reference or slice into it
// h's field refers to a slot of the caller's, which may hold a view of GA:
// here s, which does (§5.1).
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
