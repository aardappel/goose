// error: cannot clear GA while hl is still used: a reference to a slice that may point into it was stored there
// hl's element refers to p's slot, whose slice only the call maps to a view
// of GA: hl is bounded by the slot's depth, and GA is outside it (§5.1).
struct R { r: u8[:]& }
var GA: u8[>..] = [];
fn use(p: u8[:]&) {
    var hl: R[>..] = [];
    hl.push(R { r: p });
    GA.clear();
    GA.append("ZZZZZZZZ");
    print(hl[0].r);
}
fn main() {
    GA.append("abc");
    var s = GA[..];
    use(s);
}
