// error: cannot clear a while rr is still used: a reference to what may hold a reference or slice into it was stored there
// rr's field refers to bx, whose slice views a: rr leads to a's storage
// through what bx holds (§5.1).
struct B { s: u8[:] }
struct R { r: B& }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var bx = B { s: a[..] };
    var rr = R { r: bx };
    a.clear();
    a.append("ZZZZZZZZ");
    print(rr.r.s);
}
