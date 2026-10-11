// error: cannot call run: it shrinks b while b is still used, and b may refer into b (§5.1)
// What a reference the struct holds leads to may hold a view of the tail:
// the callee reads it after the shrink (§5.1).
struct H { s: const u8[:] }
struct B { h: H&, r: u8[>..] }
fn run(b: B&) {
    b.r.clear();
    print(b.h.s);
}
fn main() {
    var h = H { s: "" };
    var b = B { h: h, r: [1, 2] };
    h.s = b.r[0..1];
    run(b);
}
