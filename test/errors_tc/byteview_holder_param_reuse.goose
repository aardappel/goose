// error: storing a reference into q, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// After accepting a holder of a string from a grow-only list, put never sees
// a holder of a byte view into a grow-shrink array: the literal that would
// build one as its argument is rejected where it stands (§5.2).
var names: const u8[:][>..] = ["abcdefgh"];
struct Keep { v: const u8[:] }
fn put(h: Keep, out: Keep[>..]&) { out.push(h); }
fn main() {
    var q: i64[>..<] = [4702111234474983745];
    var keep1: Keep[>..] = [];
    var keep2: Keep[>..] = [];
    put(Keep { names[0] }, keep1);
    put(Keep { q.bytes_of() }, keep2);
    q.clear();
    q.push(6510615555426900570);
    print(keep2[0].v);
}
