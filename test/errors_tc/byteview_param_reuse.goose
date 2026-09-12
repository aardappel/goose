// Both calls share one specialization of put, so its parameter may be a byte
// view of a grow-shrink array even though the first call passes a string.
var names: const u8[:][>..<] = ["abcdefgh"];
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
