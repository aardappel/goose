// error: storing a reference into s, which holds a grow-shrink array: such a reference lives in a variable, is passed down or returned, and is never stored
// After accepting a string from a grow-only list, put must still reject a
// byte view of a grow-shrink array: their root constraints differ.
var names: const u8[:][>..] = ["abcdefgh"];
struct Keep { v: const u8[:] }
fn put(s: const u8[:], out: Keep[>..]&) { out.push(Keep { v: s }); }
fn main() {
    var q: i64[>..<] = [4702111234474983745];
    var keep1: Keep[>..] = [];
    var keep2: Keep[>..] = [];
    put(names[0], keep1);
    put(q.bytes_of(), keep2);
    q.clear();
    q.push(6510615555426900570);
    print(keep2[0].v);
}
