// error: cannot clear GA while hl is still used: a reference to a slice that may point into it was stored there
// put stores a reference to p's slot into hl's element: as when use stores
// it itself, hl is bounded by the slot's depth, and GA is outside it
// (§5.1).
struct R { r: u8[:]& }
var GA: u8[>..] = [];
fn put(hl: R[>..]&, p: u8[:]&) { hl.push(R { r: p }); }
fn use(p: u8[:]&) {
    var hl: R[>..] = [];
    put(hl, p);
    GA.clear();
    GA.append("ZZZZZZZZ");
    print(hl[0].r);
}
fn main() {
    GA.append("abc");
    var s = GA[..];
    use(s);
}
