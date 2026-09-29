// error: cannot clear GA while h is still used: a reference to a slice that may point into it was stored there
// p's slot holds a slice of a class of its own, which only the call maps to
// GA: h, which refers to that slot, is bounded by its depth, and GA is
// outside it (§5.1).
struct R { r: u8[:]& }
var GA: u8[>..] = [];
fn use(p: u8[:]&) {
    let h = R { r: p };
    GA.clear();
    GA.append("ZZZZZZZZ");
    print(h.r);
}
fn main() {
    GA.append("abc");
    var s = GA[..];
    use(s);
}
